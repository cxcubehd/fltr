#include "testing.hpp"

#include <memory>

#include "fltr/gestures/pointer_region.hpp"
#include "fltr/render/stack.hpp"
#include "fltr/widgets/binding.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

/// Every callback a Pointer widget can fire, so a test asserts a whole sequence
/// rather than one flag at a time.
struct Tally {
  int down = 0;
  int tap = 0;
  int cancel = 0;
  int enter = 0;
  int exit = 0;
};

PointerEvent mouse(PointerPhase phase, Offset position, PointerId pointer = 0) {
  return {phase, pointer, PointerDeviceKind::Mouse, position};
}

/// An interactive rectangle at a known place in a Stack, reporting into `tally`.
WidgetRef region(Tally& tally, Rect rect, Key key = Key::none()) {
  return Positioned::make({
      .key = key,
      .left = rect.left,
      .top = rect.top,
      .width = rect.width(),
      .height = rect.height(),
      .child = Pointer::make({
          .behavior = HitTestBehavior::Opaque,
          .onEnter = [&tally] { ++tally.enter; },
          .onExit = [&tally] { ++tally.exit; },
          .onTapDown = [&tally] { ++tally.down; },
          .onTap = [&tally] { ++tally.tap; },
          .onTapCancel = [&tally] { ++tally.cancel; },
      }),
  });
}

}  // namespace

// ---------------------------------------------------------------------------
// Hit-test behaviour
// ---------------------------------------------------------------------------

TEST(gestures_hit_test_behaviour_decides_what_takes_part_and_what_is_blocked) {
  PointerBinding pointers;

  // Deferring to a child it does not have puts it nowhere near the path.
  RenderPointerRegion bare(pointers, HitTestBehavior::DeferToChild);
  bare.layout(BoxConstraints::tight({20, 10}));
  HitTestResult deferred;
  CHECK(!bare.hitTest(deferred, {5, 5}));
  CHECK_EQ(deferred.path().size(), std::size_t{0});

  // Opaque takes part and stops the walk.
  RenderPointerRegion opaque(pointers, HitTestBehavior::Opaque);
  opaque.layout(BoxConstraints::tight({20, 10}));
  HitTestResult swallowed;
  CHECK(opaque.hitTest(swallowed, {5, 5}));
  CHECK_EQ(swallowed.path().size(), std::size_t{1});

  // Translucent takes part without claiming the point.
  RenderPointerRegion translucent(pointers, HitTestBehavior::Translucent);
  translucent.layout(BoxConstraints::tight({20, 10}));
  HitTestResult shared;
  CHECK(!translucent.hitTest(shared, {5, 5}));
  CHECK_EQ(shared.path().size(), std::size_t{1});
}

TEST(gestures_a_translucent_region_lets_the_one_behind_it_be_reached) {
  PointerBinding pointers;
  RenderStack stack(Alignment::topLeft(), StackFit::Expand);
  stack.addChild(std::make_unique<RenderPointerRegion>(pointers, HitTestBehavior::Opaque));
  stack.addChild(std::make_unique<RenderPointerRegion>(pointers, HitTestBehavior::Translucent));
  stack.layout(BoxConstraints::tight({50, 50}));

  HitTestResult result;
  CHECK(stack.hitTest(result, {20, 20}));
  // Topmost first, then the one it did not hide, then the stack itself.
  CHECK_EQ(result.path().size(), std::size_t{3});
  CHECK_EQ(result.path()[0].target, &stack.childAt(1));
  CHECK_EQ(result.path()[1].target, &stack.childAt(0));
}

// ---------------------------------------------------------------------------
// The arena
// ---------------------------------------------------------------------------

namespace {

struct Contender final : GestureArenaMember {
  int accepted = 0;
  int rejected = 0;
  void acceptGesture(PointerId) override { ++accepted; }
  void rejectGesture(PointerId) override { ++rejected; }
};

}  // namespace

TEST(gestures_an_uncontested_arena_resolves_the_moment_it_closes) {
  GestureArena arena;
  Contender only;
  arena.add(7, only);
  CHECK_EQ(only.accepted, 0);

  arena.close(7);
  CHECK_EQ(only.accepted, 1);
  CHECK_EQ(arena.memberCount(7), std::size_t{0});
}

TEST(gestures_a_contested_arena_waits_for_the_sweep_and_awards_the_first_member) {
  GestureArena arena;
  Contender inner;
  Contender outer;
  arena.add(1, inner);
  arena.add(1, outer);
  arena.close(1);

  // Two contenders, so closing decides nothing.
  CHECK_EQ(inner.accepted, 0);
  CHECK_EQ(outer.accepted, 0);
  CHECK_EQ(arena.memberCount(1), std::size_t{2});

  arena.sweep(1);
  CHECK_EQ(inner.accepted, 1);
  CHECK_EQ(outer.rejected, 1);
  CHECK_EQ(outer.accepted, 0);
}

TEST(gestures_rejecting_all_but_one_member_of_a_closed_arena_awards_the_survivor) {
  GestureArena arena;
  Contender first;
  Contender second;
  arena.add(1, first);
  arena.add(1, second);
  arena.close(1);

  arena.resolve(1, first, GestureDisposition::Rejected);
  CHECK_EQ(first.rejected, 1);
  CHECK_EQ(second.accepted, 1);
}

TEST(gestures_a_cancelled_arena_awards_nobody) {
  GestureArena arena;
  Contender first;
  Contender second;
  arena.add(3, first);
  arena.add(3, second);
  arena.close(3);

  arena.cancel(3);
  CHECK_EQ(first.rejected, 1);
  CHECK_EQ(second.rejected, 1);
  CHECK_EQ(first.accepted, 0);
  CHECK_EQ(second.accepted, 0);
  CHECK_EQ(arena.memberCount(3), std::size_t{0});
}

TEST(gestures_withdrawing_a_member_awards_nothing_out_of_a_destructor) {
  GestureArena arena;
  Contender dying;
  Contender survivor;
  arena.add(1, dying);
  arena.add(1, survivor);
  arena.close(1);

  // A recognizer being destroyed withdraws rather than resolving: firing a game
  // callback out of a destructor is worse than leaving the survivor to the sweep
  // it was already waiting for.
  arena.remove(dying);
  CHECK_EQ(survivor.accepted, 0);
  CHECK_EQ(dying.rejected, 0);

  arena.sweep(1);
  CHECK_EQ(survivor.accepted, 1);
}

// ---------------------------------------------------------------------------
// Tap
// ---------------------------------------------------------------------------

TEST(gestures_a_press_and_release_over_a_region_fires_tap_down_then_tap) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {10, 10}));
  CHECK_EQ(button.down, 1);
  CHECK_EQ(button.tap, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, {10, 10}));
  CHECK_EQ(button.tap, 1);
  CHECK_EQ(button.cancel, 0);
}

TEST(gestures_a_press_outside_every_region_reaches_nobody) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {150, 80}));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {150, 80}));
  CHECK_EQ(button.down, 0);
  CHECK_EQ(button.tap, 0);
  CHECK_EQ(button.cancel, 0);
}

TEST(gestures_a_press_that_travels_beyond_the_slop_is_cancelled) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {10, 10}));
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {14, 12}));
  CHECK_EQ(button.cancel, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {40, 12}));
  CHECK_EQ(button.cancel, 1);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, {40, 12}));
  CHECK_EQ(button.tap, 0);
}

TEST(gestures_a_press_that_leaves_the_region_but_not_the_slop_still_taps) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  // The recognizer follows the pointer, not the hit-test path, so a release
  // outside the region it started in is still that region's tap.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {35, 10}));
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {44, 12}));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {44, 12}));
  CHECK_EQ(button.tap, 1);
  CHECK_EQ(button.cancel, 0);
}

TEST(gestures_a_cancelled_pointer_cancels_the_press_it_had_already_won) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {10, 10}));
  CHECK_EQ(button.down, 1);

  h.binding().dispatchPointer(mouse(PointerPhase::Cancel, {10, 10}));
  CHECK_EQ(button.cancel, 1);
  CHECK_EQ(button.tap, 0);

  // And the pointer is genuinely finished: a later release is nobody's.
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {10, 10}));
  CHECK_EQ(button.tap, 0);
}

TEST(gestures_overlapping_regions_resolve_to_the_innermost_recognizer) {
  Harness h;
  Tally inner;
  Tally outer;
  ScriptedRoot root(h, [&] {
    return Pointer::make({
        .behavior = HitTestBehavior::Opaque,
        .onTapDown = [&outer] { ++outer.down; },
        .onTap = [&outer] { ++outer.tap; },
        .onTapCancel = [&outer] { ++outer.cancel; },
        .child = Align::make({
            .alignment = Alignment::topLeft(),
            .child = Pointer::make({
                .behavior = HitTestBehavior::Opaque,
                .onTapDown = [&inner] { ++inner.down; },
                .onTap = [&inner] { ++inner.tap; },
                .onTapCancel = [&inner] { ++inner.cancel; },
                .child = SizedBox::make({.size = {40, 20}}),
            }),
        }),
    });
  });
  h.frame();

  // Both are under the point, so neither may show press feedback before the
  // contest is over.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {10, 10}));
  CHECK_EQ(inner.down, 0);
  CHECK_EQ(outer.down, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, {10, 10}));
  CHECK_EQ(inner.down, 1);
  CHECK_EQ(inner.tap, 1);
  CHECK_EQ(outer.down, 0);
  CHECK_EQ(outer.tap, 0);
  // The loser never claimed the press, so it has nothing to take back.
  CHECK_EQ(outer.cancel, 0);

  // Beyond the inner region the outer one is alone and resolves at once.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {80, 10}));
  CHECK_EQ(outer.down, 1);
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {80, 10}));
  CHECK_EQ(outer.tap, 1);
  CHECK_EQ(inner.tap, 1);
}

TEST(gestures_a_region_destroyed_mid_press_withdraws_its_routes_and_arena_entry) {
  Harness h;
  Tally button;
  bool present = true;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand,
                        .children = {present ? region(button, Rect::fromLTWH(0, 0, 40, 20)) : WidgetRef{}}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {10, 10}));
  CHECK_EQ(button.down, 1);

  present = false;
  root.rebuild();
  h.frame();

  // Nothing is left to route to, and nothing fires out of a destructor.
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {10, 10}));
  CHECK_EQ(button.tap, 0);
  CHECK_EQ(button.cancel, 0);
}

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

TEST(gestures_hover_enters_and_exits_as_the_cursor_crosses_regions) {
  Harness h;
  Tally left;
  Tally right;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand,
                        .children = {region(left, Rect::fromLTWH(0, 0, 40, 20)), region(right, Rect::fromLTWH(60, 0, 40, 20))}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {10, 10}));
  CHECK_EQ(left.enter, 1);
  CHECK_EQ(right.enter, 0);

  // Moving within the same region is not a change.
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {20, 10}));
  CHECK_EQ(left.enter, 1);
  CHECK_EQ(left.exit, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {70, 10}));
  CHECK_EQ(left.exit, 1);
  CHECK_EQ(right.enter, 1);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {150, 60}));
  CHECK_EQ(right.exit, 1);
  CHECK_EQ(h.pointers().mouseTracker().hoveredCount(), std::size_t{0});
}

TEST(gestures_a_cancelled_pointer_leaves_nothing_hovered) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {10, 10}));
  CHECK_EQ(button.enter, 1);

  // The cursor left the surface: the game says so, and the highlight goes with
  // it rather than sticking until the window regains focus.
  h.binding().dispatchPointer(mouse(PointerPhase::Cancel, {10, 10}));
  CHECK_EQ(button.exit, 1);
  CHECK(!h.pointers().mouseTracker().hasCursor());
}

TEST(gestures_a_touch_pointer_never_hovers) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();

  const PointerEvent touch{PointerPhase::Down, 0, PointerDeviceKind::Touch, Offset{10, 10}};
  h.binding().dispatchPointer(touch);
  h.binding().dispatchPointer({PointerPhase::Up, 0, PointerDeviceKind::Touch, Offset{10, 10}});
  CHECK_EQ(button.tap, 1);
  CHECK_EQ(button.enter, 0);
}

TEST(gestures_hover_is_re_resolved_when_the_tree_moves_beneath_a_stationary_cursor) {
  Harness h;
  Tally button;
  Rect placement = Rect::fromLTWH(0, 0, 40, 20);
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, placement)}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {100, 50}));
  CHECK_EQ(button.enter, 0);

  // The cursor does not move; the region moves under it. The frame that lays it
  // out cannot know yet, so the answer is re-resolved at the start of the next
  // one -- which is also where a callback's setState still reaches that frame's
  // build.
  placement = Rect::fromLTWH(80, 40, 60, 40);
  root.rebuild();
  h.frame();
  const int hitTests = h.pointers().hitTestCount();
  h.frame();
  CHECK_EQ(button.enter, 1);
  CHECK_EQ(h.pointers().hitTestCount(), hitTests + 1);

  placement = Rect::fromLTWH(0, 0, 40, 20);
  root.rebuild();
  h.frame();
  h.frame();
  CHECK_EQ(button.exit, 1);
}

TEST(gestures_a_region_destroyed_while_hovered_leaves_no_dangling_reference) {
  Harness h;
  Tally button;
  bool present = true;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand,
                        .children = {present ? region(button, Rect::fromLTWH(0, 0, 40, 20)) : WidgetRef{}}});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {10, 10}));
  CHECK_EQ(button.enter, 1);

  present = false;
  root.rebuild();
  h.frame();
  h.frame();

  // Dropped without an exit: the state that callback would have updated is
  // going away with the region.
  CHECK_EQ(button.exit, 0);
  CHECK_EQ(h.pointers().mouseTracker().hoveredCount(), std::size_t{0});

  // The cursor is still tracked, and the tree it now resolves against is sound.
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {12, 12}));
  CHECK_EQ(h.pointers().mouseTracker().hoveredCount(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

TEST(gestures_a_frame_that_changes_nothing_runs_no_hit_test) {
  Harness h;
  Tally button;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand, .children = {region(button, Rect::fromLTWH(0, 0, 40, 20))}});
  });
  h.frame();
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {10, 10}));
  h.frame();

  const int hitTests = h.pointers().hitTestCount();
  h.frame();
  h.frame();
  CHECK_EQ(h.pointers().hitTestCount(), hitTests);
  CHECK(!h.binding().needsFrame());
}

TEST(gestures_dispatching_a_pointer_in_the_steady_state_allocates_nothing) {
  Harness h;
  Tally left;
  Tally right;
  ScriptedRoot root(h, [&] {
    return Stack::make({.fit = StackFit::Expand,
                        .children = {region(left, Rect::fromLTWH(0, 0, 40, 20)), region(right, Rect::fromLTWH(60, 0, 40, 20))}});
  });
  h.frame();

  // Warm the hit-test path, the tracker's buffers, the route table and the
  // arena's storage. An input-heavy frame is the common case, not the exception.
  for (int i = 0; i < 4; ++i) {
    h.binding().dispatchPointer(mouse(PointerPhase::Hover, {10, 10}));
    h.binding().dispatchPointer(mouse(PointerPhase::Hover, {70, 10}));
    h.binding().dispatchPointer(mouse(PointerPhase::Down, {70, 10}));
    h.binding().dispatchPointer(mouse(PointerPhase::Move, {71, 10}));
    h.binding().dispatchPointer(mouse(PointerPhase::Up, {71, 10}));
    h.frame();
  }

  const std::size_t before = fltrtest::allocationCount();
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {10, 10}));
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {70, 10}));
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {70, 10}));
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {71, 10}));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {71, 10}));
  CHECK_EQ(fltrtest::allocationCount() - before, std::size_t{0});
  CHECK_EQ(right.tap, 5);
}
