#include "testing.hpp"

#include <memory>

#include "fltr/gestures/pointer_region.hpp"
#include "fltr/render/stack.hpp"
#include "fltr/widgets/binding.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

PointerEvent mouse(PointerPhase phase, Offset position, PointerId pointer = 0) {
  return {phase, pointer, PointerDeviceKind::Mouse, position};
}

PointerEvent touch(PointerPhase phase, Offset position, PointerId pointer = 0) {
  return {phase, pointer, PointerDeviceKind::Touch, position};
}

WidgetRef placed(Rect rect, WidgetRef child) {
  return Positioned::make({
      .left = rect.left,
      .top = rect.top,
      .width = rect.width(),
      .height = rect.height(),
      .child = child,
  });
}

WidgetRef screen(WidgetList children) {
  return Stack::make({.fit = StackFit::Expand, .children = children});
}

constexpr Rect kBox = Rect::fromLTWH(20, 10, 60, 40);

/// `child` occupying kBox within its parent's own space.
WidgetRef inset(WidgetRef child) { return screen({placed(kBox, child)}); }

/// Everything a drag reports, so a test asserts a whole sequence rather than one
/// flag at a time.
struct DragTally {
  int down = 0;
  int start = 0;
  int update = 0;
  int end = 0;
  int cancel = 0;
  Offset travelled;
  Offset lastLocal;
  Offset velocity;
};

/// `contested` adds a tap, which is what makes the drag wait for the slop: an
/// uncontested recognizer wins the arena the moment the pointer goes down and
/// has nothing left to wait for.
WidgetRef draggable(DragTally& tally, DragAxis axis = DragAxis::Pan,
                    DragStartBehavior behavior = DragStartBehavior::Start,
                    bool contested = true) {
  return Pointer::make({
      .behavior = HitTestBehavior::Opaque,
      .onTap = contested ? Callback<void()>([] {}) : Callback<void()>{},
      .dragAxis = axis,
      .dragStartBehavior = behavior,
      .onDragDown = [&tally](const DragDownDetails&) { ++tally.down; },
      .onDragStart =
          [&tally](const DragStartDetails& d) {
            ++tally.start;
            tally.lastLocal = d.localPosition;
          },
      .onDragUpdate =
          [&tally](const DragUpdateDetails& d) {
            ++tally.update;
            tally.travelled += d.delta;
            tally.lastLocal = d.localPosition;
          },
      .onDragEnd = [&tally](const DragEndDetails& d) { ++tally.end; tally.velocity = d.velocity; },
      .onDragCancel = [&tally] { ++tally.cancel; },
  });
}

}  // namespace

// ---------------------------------------------------------------------------
// Coordinate spaces
// ---------------------------------------------------------------------------

TEST(input_a_nested_box_maps_its_own_space_into_an_ancestors) {
  auto root = std::make_unique<RenderPadding>(EdgeInsets::only(7, 3));
  auto inner = std::make_unique<RenderPadding>(EdgeInsets::only(2, 5));
  auto leaf = std::make_unique<RenderConstrainedBox>(BoxConstraints::tight({10, 10}));
  RenderBox* target = leaf.get();
  inner->setChild(std::move(leaf));
  root->setChild(std::move(inner));
  root->layout(BoxConstraints::loose({100, 100}));

  CHECK_EQ(target->localToGlobal(Offset::zero()), (Offset{9, 8}));
  CHECK_EQ(target->globalToLocal({9, 8}), Offset::zero());
  CHECK_EQ(target->localToGlobalRect(Rect::fromLTWH(0, 0, 10, 10)),
           Rect::fromLTWH(9, 8, 10, 10));

  // Stopping at an intermediate ancestor answers in that ancestor's space.
  CHECK_EQ(target->localToGlobal(Offset::zero(), target->parent()), (Offset{2, 5}));
}

TEST(input_the_ancestor_walk_agrees_with_hit_testing_through_a_transform) {
  auto root = std::make_unique<RenderPadding>(EdgeInsets::only(10, 20));
  auto transform =
      std::make_unique<RenderTransform>(Transform2D::scaling(2, 2), Alignment::topLeft());
  auto leaf = std::make_unique<RenderConstrainedBox>(BoxConstraints::tight({20, 20}));
  RenderBox* target = leaf.get();
  RenderTransform* scaled = transform.get();
  transform->setChild(std::move(leaf));
  root->setChild(std::move(transform));
  root->layout(BoxConstraints::loose({200, 200}));

  // A point at the leaf's centre lands where the scale puts it.
  CHECK_EQ(target->localToGlobal({10, 10}), (Offset{30, 40}));

  // And hit testing, which inverts the same matrix independently, agrees.
  HitTestResult path;
  scaled->hitTest(path, {20, 20});
  CHECK_EQ(target->globalToLocal({30, 40}), (Offset{10, 10}));
}

TEST(input_a_degenerate_transform_has_no_inverse_and_says_so_rather_than_guessing) {
  auto root = std::make_unique<RenderTransform>(Transform2D::scaling(0, 0), Alignment::topLeft());
  auto leaf = std::make_unique<RenderConstrainedBox>(BoxConstraints::tight({10, 10}));
  RenderBox* target = leaf.get();
  root->setChild(std::move(leaf));
  root->layout(BoxConstraints::loose({100, 100}));

  CHECK_EQ(target->globalToLocal({5, 5}), Offset::zero());
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST(input_velocity_is_fitted_over_a_window_rather_than_read_off_the_last_two_samples) {
  VelocityTracker tracker;
  for (int i = 0; i <= 5; ++i) {
    const float t = static_cast<float>(i) * 0.016f;
    tracker.addSample(t, {t * 300.0f, 0.0f});
  }
  const VelocityEstimate estimate = tracker.estimate();
  CHECK_NEAR(estimate.pixelsPerSecond.dx, 300.0f, 1.0f);
  CHECK_NEAR(estimate.pixelsPerSecond.dy, 0.0f, 0.01f);
  CHECK(estimate.confidence > 0.99f);
}

TEST(input_a_pointer_that_stopped_before_release_is_not_a_fling) {
  VelocityTracker tracker;
  // Moving fast, then held still for the last three samples.
  tracker.addSample(0.00f, {0, 0});
  tracker.addSample(0.02f, {40, 0});
  tracker.addSample(0.04f, {80, 0});
  tracker.addSample(0.06f, {80, 0});
  tracker.addSample(0.08f, {80, 0});
  tracker.addSample(0.10f, {80, 0});
  CHECK(tracker.estimate().pixelsPerSecond.dx < kMinFlingVelocity);
}

TEST(input_samples_sharing_one_instant_replace_rather_than_weight_it_twice) {
  VelocityTracker tracker;
  tracker.addSample(0.0f, {0, 0});
  tracker.addSample(0.0f, {5, 0});
  tracker.addSample(0.0f, {9, 0});
  CHECK_EQ(tracker.sampleCount(), std::size_t{1});

  tracker.addSample(0.1f, {109, 0});
  CHECK_EQ(tracker.sampleCount(), std::size_t{2});
  CHECK_NEAR(tracker.estimate().pixelsPerSecond.dx, 1000.0f, 1.0f);
}

TEST(input_samples_older_than_the_window_are_not_evidence_about_the_release) {
  VelocityTracker tracker;
  tracker.addSample(0.0f, {0, 0});
  tracker.addSample(0.5f, {1000, 0});  // outside the horizon by the time of the last
  tracker.addSample(0.55f, {1000, 0});
  tracker.addSample(0.58f, {1000, 0});
  CHECK_NEAR(tracker.estimate().pixelsPerSecond.dx, 0.0f, 1.0f);
}

TEST(input_a_velocity_is_clamped_by_magnitude_not_per_axis) {
  const Velocity v{{300.0f, 400.0f}};  // magnitude 500
  const Offset clamped = v.clampMagnitude(0.0f, 250.0f).pixelsPerSecond;
  CHECK_NEAR(clamped.distance(), 250.0f, 0.01f);
  CHECK_NEAR(clamped.dx / clamped.dy, 0.75f, 0.001f);
}

// ---------------------------------------------------------------------------
// Drag
// ---------------------------------------------------------------------------

/// PARALLEL TO FLUTTER, and surprising enough to pin down: an arena with one
/// member resolves the moment it closes, so a region whose only gesture is a
/// drag begins dragging at the down. That is what a scroll view wants -- content
/// follows the finger from the first pixel -- and the slop below is what happens
/// as soon as anything competes.
TEST(input_an_uncontested_drag_begins_at_the_down) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, draggable(tally, DragAxis::Pan, DragStartBehavior::Start, false))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  CHECK_EQ(tally.down, 1);
  CHECK_EQ(tally.start, 1);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {53, 30}));
  CHECK_EQ(tally.travelled, (Offset{3, 0}));
}

TEST(input_a_contested_drag_is_not_recognized_until_it_has_cleared_the_slop) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] { return screen({placed(kBox, draggable(tally))}); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  CHECK_EQ(tally.down, 1);
  CHECK_EQ(tally.start, 0);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {60, 30}));
  CHECK_EQ(tally.start, 0);
  CHECK_EQ(tally.update, 0);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {90, 30}));
  CHECK_EQ(tally.start, 1);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {95, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {95, 30}));
  CHECK_EQ(tally.end, 1);

  // Forty-five pixels travelled, of which the ten that happened while the drag
  // was still contending are not the drag's to report.
  CHECK_EQ(tally.travelled, (Offset{35, 0}));
}

/// PARALLEL TO FLUTTER's `computeHitSlop`: the slop above is a finger's, and a
/// mouse is held to a far smaller one. Anything else makes a control whose whole
/// travel is shorter than eighteen pixels -- a switch, say -- impossible to drag
/// with a mouse: it would sit still until the pointer had passed the far end,
/// then jump the whole way at once.
TEST(input_a_mouse_drag_clears_a_slop_its_own_size) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, draggable(tally, DragAxis::Horizontal))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {50, 30}));
  CHECK_EQ(tally.start, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {52, 30}));
  CHECK_EQ(tally.start, 1);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {57, 30}));
  CHECK_EQ(tally.travelled, (Offset{5, 0}));

  // The same two pixels from a finger are still nothing at all.
  DragTally finger;
  Harness touched;
  ScriptedRoot touchRoot(touched, [&] {
    return screen({placed(kBox, draggable(finger, DragAxis::Horizontal))});
  });
  touched.frame();

  touched.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  touched.binding().dispatchPointer(touch(PointerPhase::Move, {52, 30}));
  CHECK_EQ(finger.start, 0);
}

TEST(input_a_drag_measured_from_the_down_delivers_the_slop_it_swallowed) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, draggable(tally, DragAxis::Pan, DragStartBehavior::Down))});
  });
  h.frame();

  // The same sequence as above, so the difference between the two settings is
  // exactly the travel the other one swallowed.
  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {60, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {90, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {95, 30}));
  CHECK_EQ(tally.start, 1);
  CHECK_EQ(tally.travelled, (Offset{45, 0}));
}

TEST(input_a_constrained_drag_reports_only_its_own_axis) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox,
                          draggable(tally, DragAxis::Vertical, DragStartBehavior::Start, false))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {70, 35}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {30, 40}));
  // Forty pixels of sideways travel, and the consumer never has to discard it.
  CHECK_EQ(tally.travelled, (Offset{0, 10}));
}

TEST(input_nested_drags_are_decided_by_which_axis_cleared_the_slop) {
  Harness h;
  DragTally horizontal;
  DragTally vertical;
  ScriptedRoot root(h, [&] {
    return screen({placed(Rect::fromLTWH(0, 0, 200, 100),
                          Pointer::make({
                              .behavior = HitTestBehavior::Opaque,
                              .dragAxis = DragAxis::Horizontal,
                              .onDragStart = [&horizontal](
                                                 const DragStartDetails&) { ++horizontal.start; },
                              .child = inset(Pointer::make({
                                  .behavior = HitTestBehavior::Opaque,
                                  .dragAxis = DragAxis::Vertical,
                                  .onDragStart = [&vertical](
                                                     const DragStartDetails&) { ++vertical.start; },
                              })),
                          }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  CHECK_EQ(horizontal.start, 0);
  CHECK_EQ(vertical.start, 0);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {50 + 3 * kTouchSlop, 32}));
  CHECK_EQ(horizontal.start, 1);
  CHECK_EQ(vertical.start, 0);
}

TEST(input_a_drag_reports_positions_in_the_space_of_the_region_that_owns_it) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] { return screen({placed(kBox, draggable(tally))}); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {95, 30}));
  CHECK_EQ(tally.start, 1);
  // The region sits at (20, 10), so the global (95, 30) is local (75, 20).
  CHECK_EQ(tally.lastLocal, (Offset{75, 20}));
}

/// The limitation M5 recorded: HitTestResult keeps resolved local positions
/// rather than transforms, so a gesture inside a rotated subtree had no way back
/// to its own space. The ancestor walk lifts it -- the recognizer asks its
/// region, not the hit-test path.
TEST(input_a_drag_inside_a_scaled_subtree_reports_its_own_space) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] {
    return screen({placed(Rect::fromLTWH(0, 0, 100, 100),
                          Transform::make({
                              .transform = Transform2D::scaling(2, 2),
                              .origin = Alignment::topLeft(),
                              .child = draggable(tally, DragAxis::Pan, DragStartBehavior::Start,
                                                 false),
                          }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {40, 60}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {60, 80}));
  CHECK_EQ(tally.start, 1);
  CHECK_EQ(tally.lastLocal, (Offset{30, 40}));
}

TEST(input_a_tap_inside_a_draggable_region_is_still_a_tap) {
  Harness h;
  DragTally drag;
  int taps = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onTap = [&taps] { ++taps; },
                                    .onDragUpdate = [&drag](const DragUpdateDetails&) { ++drag.update; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {52, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {52, 30}));
  CHECK_EQ(taps, 1);
  CHECK_EQ(drag.update, 0);
}

TEST(input_a_drag_out_of_a_tappable_region_cancels_the_tap_and_keeps_the_drag) {
  Harness h;
  DragTally drag;
  int taps = 0;
  int cancels = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onTapDown = [] {},
                                    .onTap = [&taps] { ++taps; },
                                    .onTapCancel = [&cancels] { ++cancels; },
                                    .onDragStart = [&drag](const DragStartDetails&) { ++drag.start; },
                                    .onDragEnd = [&drag](const DragEndDetails&) { ++drag.end; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {130, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {130, 30}));
  CHECK_EQ(drag.start, 1);
  CHECK_EQ(drag.end, 1);
  CHECK_EQ(taps, 0);
}

TEST(input_a_slow_release_reports_no_velocity_at_all) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] { return screen({placed(kBox, draggable(tally))}); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {30, 30}));
  for (int i = 1; i <= 6; ++i) {
    h.frame(0.016f);
    h.binding().dispatchPointer(touch(PointerPhase::Move, {30.0f + static_cast<float>(i) * 10.0f, 30}));
  }
  h.binding().dispatchPointer(touch(PointerPhase::Up, {90, 30}));
  CHECK_EQ(tally.end, 1);
  // 10 pixels per 16ms is 625 px/s, comfortably a fling.
  CHECK(tally.velocity.dx > kMinFlingVelocity);

  DragTally slow;
  Harness h2;
  ScriptedRoot root2(h2, [&] { return screen({placed(kBox, draggable(slow))}); });
  h2.frame();
  h2.binding().dispatchPointer(touch(PointerPhase::Down, {30, 30}));
  for (int i = 1; i <= 6; ++i) {
    h2.frame(0.2f);
    h2.binding().dispatchPointer(touch(PointerPhase::Move, {30.0f + static_cast<float>(i) * 10.0f, 30}));
  }
  h2.binding().dispatchPointer(touch(PointerPhase::Up, {90, 30}));
  CHECK_EQ(slow.end, 1);
  CHECK_EQ(slow.velocity, Offset::zero());
}

// ---------------------------------------------------------------------------
// Long press
// ---------------------------------------------------------------------------

TEST(input_a_long_press_fires_from_the_clock_while_the_pointer_holds_still) {
  Harness h;
  int presses = 0;
  int taps = 0;
  Offset where;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onTap = [&taps] { ++taps; },
                                    .onLongPress =
                                        [&](const LongPressStartDetails& d) {
                                          ++presses;
                                          where = d.localPosition;
                                        },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  // No pointer event arrives while a finger rests, so only the frame can fire
  // this -- and it asks for one until it has.
  CHECK(h.binding().needsFrame());
  h.frame(0.3f);
  CHECK_EQ(presses, 0);
  h.frame(0.3f);
  CHECK_EQ(presses, 1);
  CHECK_EQ(where, (Offset{30, 20}));

  // Winning the pointer is what stops the release from also being a tap.
  h.binding().dispatchPointer(touch(PointerPhase::Up, {50, 30}));
  CHECK_EQ(taps, 0);
  CHECK(!h.binding().pointers().hasPendingTimeouts());
}

TEST(input_lifting_before_the_deadline_is_a_tap_and_not_a_long_press) {
  Harness h;
  int presses = 0;
  int taps = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onTap = [&taps] { ++taps; },
                                    .onLongPress = [&presses](const LongPressStartDetails&) { ++presses; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  h.frame(0.2f);
  h.binding().dispatchPointer(touch(PointerPhase::Up, {50, 30}));
  CHECK_EQ(taps, 1);
  CHECK_EQ(presses, 0);

  // The deadline was withdrawn with the gesture, so passing it changes nothing.
  h.frame(1.0f);
  CHECK_EQ(presses, 0);
}

TEST(input_drifting_before_the_deadline_abandons_the_long_press) {
  Harness h;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onLongPress = [&presses](const LongPressStartDetails&) { ++presses; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {30, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {30 + 2 * kTouchSlop, 30}));
  h.frame(1.0f);
  CHECK_EQ(presses, 0);
}

TEST(input_a_long_press_reports_movement_after_it_is_recognized) {
  Harness h;
  int moves = 0;
  Offset fromOrigin;
  int ends = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onLongPress = [](const LongPressStartDetails&) {},
                                    .onLongPressMoveUpdate =
                                        [&](const LongPressMoveUpdateDetails& d) {
                                          ++moves;
                                          fromOrigin = d.offsetFromOrigin;
                                        },
                                    .onLongPressEnd = [&ends](const LongPressEndDetails&) { ++ends; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {30, 30}));
  h.frame(0.6f);
  h.binding().dispatchPointer(touch(PointerPhase::Move, {70, 40}));
  CHECK_EQ(moves, 1);
  CHECK_EQ(fromOrigin, (Offset{40, 10}));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {70, 40}));
  CHECK_EQ(ends, 1);
}

// ---------------------------------------------------------------------------
// Double tap
// ---------------------------------------------------------------------------

TEST(input_two_taps_in_the_same_place_are_a_double_tap) {
  Harness h;
  int doubles = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onDoubleTap = [&doubles] { ++doubles; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}, 1));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {50, 30}, 1));
  h.frame(0.1f);
  h.binding().dispatchPointer(touch(PointerPhase::Down, {52, 31}, 2));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {52, 31}, 2));
  CHECK_EQ(doubles, 1);
}

TEST(input_a_second_tap_that_arrives_too_late_is_two_single_taps) {
  Harness h;
  int doubles = 0;
  int taps = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onTap = [&taps] { ++taps; },
                                    .onDoubleTap = [&doubles] { ++doubles; },
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}, 1));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {50, 30}, 1));
  // Held open: until the wait is over the first tap may still be half of one.
  CHECK_EQ(taps, 0);

  h.frame(0.4f);
  CHECK_EQ(taps, 1);

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}, 2));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {50, 30}, 2));
  h.frame(0.4f);
  CHECK_EQ(taps, 2);
  CHECK_EQ(doubles, 0);
}

TEST(input_a_second_tap_too_far_away_is_not_the_same_gesture) {
  Harness h;
  int doubles = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(Rect::fromLTWH(0, 0, 200, 100),
                          Pointer::make({
                              .behavior = HitTestBehavior::Opaque,
                              .onDoubleTap = [&doubles] { ++doubles; },
                          }))});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {10, 10}, 1));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {10, 10}, 1));
  h.binding().dispatchPointer(touch(PointerPhase::Down, {180, 90}, 2));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {180, 90}, 2));
  CHECK_EQ(doubles, 0);
}

// ---------------------------------------------------------------------------
// Pointer signals
// ---------------------------------------------------------------------------

TEST(input_a_signal_is_offered_innermost_first_until_one_consumes_it) {
  Harness h;
  int outer = 0;
  int inner = 0;
  bool innerConsumes = true;
  Offset innerLocal;

  ScriptedRoot root(h, [&] {
    return screen({placed(Rect::fromLTWH(0, 0, 200, 100),
                          Pointer::make({
                              .behavior = HitTestBehavior::Opaque,
                              .onSignal =
                                  [&outer](const PointerSignalEvent&) {
                                    ++outer;
                                    return true;
                                  },
                              .child = inset(Pointer::make({
                                  .behavior = HitTestBehavior::Opaque,
                                  .onSignal =
                                      [&](const PointerSignalEvent& e) {
                                        ++inner;
                                        innerLocal = e.localPosition;
                                        return innerConsumes;
                                      },
                              })),
                          }))});
  });
  h.frame();

  const PointerSignalEvent wheel{.position = {50, 30}, .delta = {0, -40}};
  CHECK(h.binding().dispatchSignal(wheel));
  CHECK_EQ(inner, 1);
  CHECK_EQ(outer, 0);
  CHECK_EQ(innerLocal, (Offset{30, 20}));

  // Declining chains outward, which is what a scrolled-to-the-end list does.
  innerConsumes = false;
  CHECK(h.binding().dispatchSignal(wheel));
  CHECK_EQ(inner, 2);
  CHECK_EQ(outer, 1);
}

TEST(input_a_signal_over_nothing_is_reported_unconsumed_so_the_game_can_have_it) {
  Harness h;
  int seen = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onSignal =
                                        [&seen](const PointerSignalEvent&) {
                                          ++seen;
                                          return true;
                                        },
                                }))});
  });
  h.frame();

  CHECK(!h.binding().dispatchSignal({.position = {150, 80}, .delta = {0, -40}}));
  CHECK_EQ(seen, 0);
}

TEST(input_a_signal_never_enters_the_arena) {
  Harness h;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onTap = [] {},
                                    .onSignal = [](const PointerSignalEvent&) { return true; },
                                }))});
  });
  h.frame();

  h.binding().dispatchSignal({.position = {50, 30}, .delta = {0, -40}});
  CHECK_EQ(h.pointers().arena().memberCount(0), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Cursors
// ---------------------------------------------------------------------------

TEST(input_the_innermost_region_with_an_opinion_decides_the_cursor) {
  Harness h;
  ScriptedRoot root(h, [&] {
    return screen({placed(Rect::fromLTWH(0, 0, 200, 100),
                          Pointer::make({
                              .behavior = HitTestBehavior::Opaque,
                              .cursor = MouseCursor::Text,
                              .child = inset(Pointer::make({
                                  .behavior = HitTestBehavior::Opaque,
                                  .cursor = MouseCursor::Click,
                              })),
                          }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {50, 30}));
  CHECK_EQ(h.binding().cursor(), MouseCursor::Click);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {150, 80}));
  CHECK_EQ(h.binding().cursor(), MouseCursor::Text);

  // Off the tree entirely, and there is nothing left with an opinion.
  h.binding().dispatchPointer(mouse(PointerPhase::Cancel, {150, 80}));
  CHECK_EQ(h.binding().cursor(), MouseCursor::Basic);
}

TEST(input_a_region_that_only_asks_for_a_cursor_holds_no_recognizers) {
  Harness h;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .cursor = MouseCursor::Grab,
                                }))});
  });
  h.frame();

  const Element& element = elementFor(h.rootElement(), widgetTypeOf<Pointer>());
  const auto* region = static_cast<const RenderPointerRegion*>(element.renderObject());
  CHECK_EQ(region->recognizerCount(), std::size_t{0});
  CHECK(region->tracksMouse());
}

TEST(input_recognizers_appear_and_disappear_with_the_callbacks_that_want_them) {
  Harness h;
  bool interactive = false;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, interactive ? Pointer::make({
                                                  .behavior = HitTestBehavior::Opaque,
                                                  .onTap = [] {},
                                                  .onDragUpdate = [](const DragUpdateDetails&) {},
                                              })
                                            : Pointer::make({.behavior = HitTestBehavior::Opaque}))});
  });
  h.frame();

  const Element& element = elementFor(h.rootElement(), widgetTypeOf<Pointer>());
  const auto* region = static_cast<const RenderPointerRegion*>(element.renderObject());
  CHECK_EQ(region->recognizerCount(), std::size_t{0});

  interactive = true;
  root.rebuild();
  h.frame();
  CHECK_EQ(region->recognizerCount(), std::size_t{2});

  interactive = false;
  root.rebuild();
  h.frame();
  CHECK_EQ(region->recognizerCount(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

TEST(input_a_gesture_answers_only_to_the_buttons_it_was_given) {
  Harness h;
  int primary = 0;
  int secondary = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(Rect::fromLTWH(0, 0, 200, 100),
                          Pointer::make({
                              .behavior = HitTestBehavior::Opaque,
                              .buttons = PointerButton::Secondary,
                              .onTap = [&secondary] { ++secondary; },
                              .child = inset(Pointer::make({
                                  .behavior = HitTestBehavior::Translucent,
                                  .onTap = [&primary] { ++primary; },
                              })),
                          }))});
  });
  h.frame();

  PointerEvent down = mouse(PointerPhase::Down, {50, 30});
  down.buttons = PointerButton::Secondary;
  PointerEvent up = mouse(PointerPhase::Up, {50, 30});
  up.buttons = PointerButton::Secondary;
  h.binding().dispatchPointer(down);
  h.binding().dispatchPointer(up);
  CHECK_EQ(secondary, 1);
  CHECK_EQ(primary, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {50, 30}));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {50, 30}));
  CHECK_EQ(primary, 1);
  CHECK_EQ(secondary, 1);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

namespace {

/// Records what it saw and optionally swallows it, which is the whole contract a
/// key handler has.
class Recorder final : public KeyHandler {
public:
  explicit Recorder(bool consumes) : consumes_(consumes) {}

  bool handleKey(const KeyEvent& event) override {
    ++count;
    last = event;
    return consumes_;
  }

  int count = 0;
  KeyEvent last;

private:
  bool consumes_;
};

}  // namespace

TEST(input_the_keyboard_tracks_what_is_held_without_anyone_listening) {
  Harness h;
  KeyboardBinding& keyboard = h.binding().keyboard();

  CHECK(!h.binding().dispatchKey({KeyEventType::Down, PhysicalKey::KeyW, LogicalKey::KeyW}));
  CHECK(keyboard.isPressed(PhysicalKey::KeyW));
  CHECK_EQ(keyboard.pressedCount(), std::size_t{1});

  // A repeat is not a second press.
  h.binding().dispatchKey({KeyEventType::Repeat, PhysicalKey::KeyW, LogicalKey::KeyW});
  CHECK_EQ(keyboard.pressedCount(), std::size_t{1});

  h.binding().dispatchKey({KeyEventType::Up, PhysicalKey::KeyW, LogicalKey::KeyW});
  CHECK(!keyboard.isPressed(PhysicalKey::KeyW));
}

TEST(input_a_handler_that_consumes_a_key_ends_the_walk) {
  KeyboardBinding keyboard;
  Recorder first(false);
  Recorder greedy(true);
  Recorder never(false);
  keyboard.addHandler(first);
  keyboard.addHandler(greedy);
  keyboard.addHandler(never);

  CHECK(keyboard.dispatch({KeyEventType::Down, PhysicalKey::Escape, LogicalKey::Escape}));
  CHECK_EQ(first.count, 1);
  CHECK_EQ(greedy.count, 1);
  CHECK_EQ(never.count, 0);

  keyboard.removeHandler(greedy);
  CHECK(!keyboard.dispatch({KeyEventType::Down, PhysicalKey::Escape, LogicalKey::Escape}));
  CHECK_EQ(never.count, 1);
}

TEST(input_modifiers_and_the_produced_character_travel_with_the_event) {
  KeyboardBinding keyboard;
  Recorder recorder(false);
  keyboard.addHandler(recorder);

  keyboard.dispatch({.type = KeyEventType::Down,
                     .physical = PhysicalKey::KeyA,
                     .logical = LogicalKey::KeyA,
                     .modifiers = KeyModifier::Shift | KeyModifier::Control,
                     .character = U'A'});
  CHECK(recorder.last.modifiers.has(KeyModifier::Shift));
  CHECK(recorder.last.modifiers.has(KeyModifier::Control));
  CHECK(!recorder.last.modifiers.has(KeyModifier::Alt));
  CHECK_EQ(recorder.last.character, U'A');
  CHECK_EQ(keyboard.modifiers(), (KeyModifier::Shift | KeyModifier::Control));
}

TEST(input_losing_the_window_releases_everything_held) {
  KeyboardBinding keyboard;
  keyboard.dispatch({KeyEventType::Down, PhysicalKey::KeyW, LogicalKey::KeyW});
  keyboard.dispatch({KeyEventType::Down, PhysicalKey::ShiftLeft, LogicalKey::Shift});
  CHECK_EQ(keyboard.pressedCount(), std::size_t{2});

  keyboard.clearPressed();
  CHECK_EQ(keyboard.pressedCount(), std::size_t{0});
  CHECK(!keyboard.modifiers().any());
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

TEST(input_a_steady_frame_with_a_drag_in_flight_allocates_nothing) {
  Harness h;
  DragTally tally;
  ScriptedRoot root(h, [&] { return screen({placed(kBox, draggable(tally))}); });
  h.frame();

  // Two presses, so the route, timeout and firing lists have all reached the
  // high-water mark a steady gesture needs before anything is counted.
  for (int warmup = 0; warmup < 2; ++warmup) {
    h.binding().dispatchPointer(touch(PointerPhase::Down, {30, 30}));
    h.binding().dispatchPointer(touch(PointerPhase::Move, {110, 30}));
    h.frame(0.016f);
    h.binding().dispatchPointer(touch(PointerPhase::Up, {110, 30}));
  }

  h.binding().dispatchPointer(touch(PointerPhase::Down, {30, 30}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {110, 30}));
  CHECK_EQ(tally.start, 3);

  const std::size_t before = allocationCount();
  for (int i = 0; i < 30; ++i) {
    h.frame(0.016f);
    h.binding().dispatchPointer(
        touch(PointerPhase::Move, {110.0f + static_cast<float>(i), 30}));
  }
  CHECK_EQ(allocationCount(), before);
}

TEST(input_a_frame_that_fires_a_deadline_allocates_nothing) {
  Harness h;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onLongPress = [&presses](const LongPressStartDetails&) { ++presses; },
                                }))});
  });
  h.frame();

  const auto press = [&] {
    h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
    h.frame(0.6f);
    h.binding().dispatchPointer(touch(PointerPhase::Up, {50, 30}));
  };
  press();
  CHECK_EQ(presses, 1);

  const std::size_t before = allocationCount();
  press();
  CHECK_EQ(presses, 2);
  CHECK_EQ(allocationCount(), before);
}

TEST(input_a_tree_with_no_gestures_pending_asks_for_no_frames) {
  Harness h;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, Pointer::make({
                                    .behavior = HitTestBehavior::Opaque,
                                    .onLongPress = [&presses](const LongPressStartDetails&) { ++presses; },
                                }))});
  });
  h.frame();
  CHECK(!h.binding().needsFrame());

  h.binding().dispatchPointer(touch(PointerPhase::Down, {50, 30}));
  CHECK(h.binding().needsFrame());

  h.frame(0.6f);
  CHECK_EQ(presses, 1);
  h.frame();
  CHECK(!h.binding().needsFrame());
}
