#include "testing.hpp"

#include <memory>
#include <string>

#include "fltr/components/button.hpp"
#include "fltr/components/progress.hpp"
#include "fltr/components/slider.hpp"
#include "fltr/components/toggle.hpp"
#include "fltr/harness.hpp"
#include "fltr/widgets/binding.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

PointerEvent mouse(PointerPhase phase, Offset position, PointerId pointer = 0) {
  return {phase, pointer, PointerDeviceKind::Mouse, position};
}

KeyEvent down(LogicalKey key) { return {.type = KeyEventType::Down, .logical = key}; }
KeyEvent up(LogicalKey key) { return {.type = KeyEventType::Up, .logical = key}; }
KeyEvent repeat(LogicalKey key) { return {.type = KeyEventType::Repeat, .logical = key}; }

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
constexpr Offset kCentre{50, 30};
constexpr Offset kAway{150, 80};

WidgetRef fill() { return SizedBox::make({.size = kBox.size()}); }

class Probe;

/// Reads what a component publishes, which is the only way a visual sees it. The
/// pointers it hands back belong to the component's State and outlive the build
/// that named them, unlike the scope config carrying them.
class ProbeState final : public State<Probe> {
public:
  WidgetRef build(BuildContext& context) override;
};

class Probe final : public Configure<Probe, StatefulWidget> {
public:
  struct Args {
    Key key;
    WidgetStatesController** states = nullptr;
    ValueListenable<float>** fraction = nullptr;
    WidgetRef child;
  };

  explicit Probe(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Probe"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<State<Probe>> createState() const { return std::make_unique<ProbeState>(); }

private:
  Args args_;
};

WidgetRef ProbeState::build(BuildContext& context) {
  if (widget().args().states) *widget().args().states = ComponentScope::statesOf(context);
  if (widget().args().fraction) *widget().args().fraction = ComponentScope::fractionOf(context);
  return widget().args().child;
}

void tap(Harness& h, Offset at = kCentre) {
  h.binding().dispatchPointer(mouse(PointerPhase::Down, at));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, at));
}

RenderPointerRegion& regionOf(Harness& h) {
  Element& element = elementFor(h.rootElement(), widgetTypeOf<Pointer>());
  RenderBox* box = element.renderObject();
  FLTR_EXPECTS(box != nullptr, "the pointer region has no render object");
  return *box->asPointerRegion();
}

}  // namespace

// ---------------------------------------------------------------------------
// The states themselves
// ---------------------------------------------------------------------------

TEST(components_a_states_controller_notifies_only_when_the_set_actually_moves) {
  WidgetStatesController states;
  int changes = 0;
  Subscription watching;
  states.subscribe(
      watching, [](void* count) { ++*static_cast<int*>(count); }, &changes);

  states.update(WidgetState::Hovered, true);
  CHECK_EQ(changes, 1);
  states.update(WidgetState::Hovered, true);
  CHECK_EQ(changes, 1);

  states.update(WidgetState::Pressed, true);
  CHECK_EQ(changes, 2);
  CHECK(states.has(WidgetState::Hovered));
  CHECK(states.has(WidgetState::Pressed));

  states.update(WidgetState::Hovered, false);
  CHECK_EQ(changes, 3);
  CHECK(!states.has(WidgetState::Hovered));
  CHECK(states.has(WidgetState::Pressed));

  states.update(WidgetState::Selected, false);
  CHECK_EQ(changes, 3);
}

TEST(components_a_controller_destroyed_under_a_live_component_is_let_go_of) {
  Harness h;
  auto states = std::make_unique<WidgetStatesController>();
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [&presses] { ++presses; },
                                    .statesController = states.get(),
                                    .child = fill(),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, kCentre));
  CHECK(states->has(WidgetState::Hovered));

  states.reset();
  // Still mounted and still receiving; what it no longer has is anywhere to
  // publish. The subscription is what says so -- there is no second pointer to
  // keep in step.
  tap(h);
  CHECK_EQ(presses, 1);
}

TEST(components_a_widget_that_goes_on_naming_a_destroyed_controller_is_trapped) {
  Harness h;
  auto states = std::make_unique<WidgetStatesController>();
  WidgetStatesController* named = states.get();
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [] {},
                                    .statesController = named,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  // Surviving the events in flight is one thing; a *build* that still names the
  // dead controller is the consumer breaking the outlive rule, and re-binding
  // would be a write into freed memory rather than a diagnosis.
  states.reset();
  root.rebuild();
  CHECK_THROWS(h.frame());
}

// ---------------------------------------------------------------------------
// The two ways to read a component's state
// ---------------------------------------------------------------------------

TEST(components_a_states_builder_rebuilds_its_own_subtree_and_nothing_above_it) {
  Harness h;
  int outer = 0;
  int inner = 0;
  ScriptedRoot root(h, [&] {
    ++outer;
    return screen({placed(kBox, RawButton::make({
                                    .child = StatesBuilder::make({
                                        .builder =
                                            [&inner](BuildContext&, WidgetStates) {
                                              ++inner;
                                              return fill();
                                            },
                                    }),
                                }))});
  });
  h.frame();
  CHECK_EQ(outer, 1);
  CHECK_EQ(inner, 1);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, kCentre));
  h.frame();
  CHECK_EQ(inner, 2);
  CHECK_EQ(outer, 1);

  // The component itself never rebuilds for its own state: it publishes and is
  // done, which is what makes the observable path cost nothing.
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, kAway));
  h.frame();
  CHECK_EQ(inner, 3);
  CHECK_EQ(outer, 1);
}

TEST(components_a_states_builder_outside_a_component_builds_with_nothing_set) {
  Harness h;
  WidgetStates seen{WidgetState::Hovered};
  int builds = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, StatesBuilder::make({
                                    .builder =
                                        [&](BuildContext&, WidgetStates states) {
                                          ++builds;
                                          seen = states;
                                          return fill();
                                        },
                                }))});
  });
  h.frame();

  CHECK_EQ(builds, 1);
  CHECK(!seen.any());
}

TEST(components_the_observable_path_hands_back_the_component_s_own_controller) {
  Harness h;
  WidgetStatesController mine;
  WidgetStatesController* published = nullptr;
  ValueListenable<float>* fraction = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .statesController = &mine,
                                    .child = Probe::make({
                                        .states = &published,
                                        .fraction = &fraction,
                                        .child = fill(),
                                    }),
                                }))});
  });
  h.frame();

  CHECK(published == &mine);
  // A button has no position to draw itself from; a toggle and a slider do.
  CHECK(fraction == nullptr);
}

// ---------------------------------------------------------------------------
// IgnorePointer and AbsorbPointer
// ---------------------------------------------------------------------------

TEST(components_ignoring_hides_a_subtree_from_the_pointer_and_absorbing_swallows_it) {
  PointerBinding pointers;

  RenderIgnorePointer ignoring(true);
  ignoring.setChild(std::make_unique<RenderPointerRegion>(pointers, HitTestBehavior::Opaque));
  ignoring.layout(BoxConstraints::tight({20, 10}));
  HitTestResult hidden;
  CHECK(!ignoring.hitTest(hidden, {5, 5}));
  CHECK_EQ(hidden.path().size(), std::size_t{0});

  ignoring.setIgnoring(false);
  HitTestResult passed;
  CHECK(ignoring.hitTest(passed, {5, 5}));
  CHECK_EQ(passed.path().size(), std::size_t{2});

  RenderAbsorbPointer absorbing(true);
  absorbing.setChild(std::make_unique<RenderPointerRegion>(pointers, HitTestBehavior::Opaque));
  absorbing.layout(BoxConstraints::tight({20, 10}));
  HitTestResult swallowed;
  // Hit, and the only thing hit: the region below never learns the pointer was
  // there, and neither does anything behind.
  CHECK(absorbing.hitTest(swallowed, {5, 5}));
  CHECK_EQ(swallowed.path().size(), std::size_t{1});
}

TEST(components_a_disabled_component_swallows_the_pointer_rather_than_leaking_it) {
  Harness h;
  int behind = 0;
  int presses = 0;
  bool passThrough = false;
  ScriptedRoot root(h, [&] {
    WidgetRef button = RawButton::make({
        .onPressed = [&presses] { ++presses; },
        .enabled = false,
        .child = fill(),
    });
    return screen({
        placed(kBox, Pointer::make({
                         .behavior = HitTestBehavior::Opaque,
                         .onTap = [&behind] { ++behind; },
                     })),
        placed(kBox, passThrough ? IgnorePointer::make({.child = button}) : button),
    });
  });
  h.frame();

  tap(h);
  CHECK_EQ(presses, 0);
  CHECK_EQ(behind, 0);

  // Wanting the other thing is one widget away, and is the consumer's to say.
  passThrough = true;
  root.rebuild();
  h.frame();
  tap(h);
  CHECK_EQ(presses, 0);
  CHECK_EQ(behind, 1);
}

// ---------------------------------------------------------------------------
// RawButton
// ---------------------------------------------------------------------------

TEST(components_a_button_reports_hover_and_press_and_fires_on_release) {
  Harness h;
  WidgetStatesController states;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [&presses] { ++presses; },
                                    .statesController = &states,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, kCentre));
  CHECK(states.has(WidgetState::Hovered));
  CHECK(!states.has(WidgetState::Pressed));

  h.binding().dispatchPointer(mouse(PointerPhase::Down, kCentre));
  CHECK(states.has(WidgetState::Pressed));
  CHECK_EQ(presses, 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, kCentre));
  CHECK(!states.has(WidgetState::Pressed));
  CHECK_EQ(presses, 1);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, kAway));
  CHECK(!states.has(WidgetState::Hovered));
}

TEST(components_a_press_that_travels_off_the_button_is_let_go_of_and_never_fires) {
  Harness h;
  WidgetStatesController states;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [&presses] { ++presses; },
                                    .statesController = &states,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, kCentre));
  CHECK(states.has(WidgetState::Pressed));

  h.binding().dispatchPointer(mouse(PointerPhase::Move, kAway));
  CHECK(!states.has(WidgetState::Pressed));

  h.binding().dispatchPointer(mouse(PointerPhase::Up, kAway));
  CHECK_EQ(presses, 0);
}

TEST(components_a_button_activates_from_the_keyboard_on_the_release) {
  Harness h;
  FocusNode node;
  WidgetStatesController states;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({.child = screen({placed(kBox, RawButton::make({
                                                              .onPressed = [&presses] { ++presses; },
                                                              .statesController = &states,
                                                              .focusNode = &node,
                                                              .autofocus = true,
                                                              .child = fill(),
                                                          }))})});
  });
  h.frame();

  CHECK(node.hasPrimaryFocus());
  CHECK(states.has(WidgetState::Focused));

  CHECK(h.binding().dispatchKey(down(LogicalKey::Space)));
  CHECK(states.has(WidgetState::Pressed));
  CHECK_EQ(presses, 0);

  // Held is one press, not a stream of them.
  CHECK(h.binding().dispatchKey(repeat(LogicalKey::Space)));
  CHECK_EQ(presses, 0);

  CHECK(h.binding().dispatchKey(up(LogicalKey::Space)));
  CHECK(!states.has(WidgetState::Pressed));
  CHECK_EQ(presses, 1);

  CHECK(h.binding().dispatchKey(down(LogicalKey::Enter)));
  CHECK(h.binding().dispatchKey(up(LogicalKey::Enter)));
  CHECK_EQ(presses, 2);
}

TEST(components_losing_the_focus_mid_press_lets_go_without_firing) {
  Harness h;
  FocusNode here;
  FocusNode elsewhere;
  WidgetStatesController states;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .child = screen({
            placed(kBox, RawButton::make({
                             .onPressed = [&presses] { ++presses; },
                             .statesController = &states,
                             .focusNode = &here,
                             .autofocus = true,
                             .child = fill(),
                         })),
            placed(Rect::fromLTWH(100, 10, 40, 40),
                   Focus::make({.node = &elsewhere, .child = SizedBox::make({.size = {40, 40}})})),
        }),
    });
  });
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::Space)));
  CHECK(states.has(WidgetState::Pressed));

  elsewhere.requestFocus();
  CHECK(!states.has(WidgetState::Focused));
  CHECK(!states.has(WidgetState::Pressed));
  CHECK_EQ(presses, 0);

  // The release lands wherever the focus went, and is not this button's.
  h.binding().dispatchKey(up(LogicalKey::Space));
  CHECK_EQ(presses, 0);
}

TEST(components_a_disabled_button_takes_no_input_and_the_keyboard_walks_past_it) {
  Harness h;
  FocusNode node;
  FocusNode other;
  WidgetStatesController states;
  int presses = 0;
  bool enabled = true;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .child = screen({
            placed(kBox, RawButton::make({
                             .onPressed = [&presses] { ++presses; },
                             .enabled = enabled,
                             .statesController = &states,
                             .focusNode = &node,
                             .autofocus = true,
                             .child = fill(),
                         })),
            placed(Rect::fromLTWH(100, 10, 40, 40),
                   Focus::make({.node = &other, .child = SizedBox::make({.size = {40, 40}})})),
        }),
    });
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, kCentre));
  CHECK(node.hasPrimaryFocus());
  CHECK(states.has(WidgetState::Hovered));

  enabled = false;
  root.rebuild();
  h.frame();

  CHECK(states.has(WidgetState::Disabled));
  CHECK(!states.has(WidgetState::Hovered));
  CHECK(!node.isFocusable());
  CHECK(!node.hasFocus());

  tap(h);
  CHECK_EQ(presses, 0);

  // Tab reaches the only thing left that can be reached.
  CHECK(h.binding().dispatchKey(down(LogicalKey::Tab)));
  CHECK(other.hasPrimaryFocus());
}

TEST(components_a_long_press_keeps_the_button_pressed_and_suppresses_the_tap) {
  Harness h;
  WidgetStatesController states;
  int presses = 0;
  int longPresses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [&presses] { ++presses; },
                                    .onLongPress = [&longPresses] { ++longPresses; },
                                    .statesController = &states,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, kCentre));
  // Nothing yet, and this is the price of wanting two gestures: `onTapDown`
  // fires when the tap *wins*, and with a long press also contending it has not.
  // A button with only a press is the sole contender and is pressed at the down.
  CHECK(!states.has(WidgetState::Pressed));

  h.frame(0.3f);
  h.frame(0.3f);
  CHECK_EQ(longPresses, 1);
  // The finger is still down, and that is what the state means.
  CHECK(states.has(WidgetState::Pressed));

  h.binding().dispatchPointer(mouse(PointerPhase::Up, kCentre));
  CHECK(!states.has(WidgetState::Pressed));
  CHECK_EQ(presses, 0);
}

TEST(components_a_button_holds_only_the_recognizers_its_call_site_asked_for) {
  Harness h;
  bool wantsLongPress = false;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [] {},
                                    .onLongPress = wantsLongPress ? Callback<void()>([] {})
                                                                  : Callback<void()>{},
                                    .child = fill(),
                                }))});
  });
  h.frame();
  CHECK_EQ(regionOf(h).recognizerCount(), std::size_t{1});

  wantsLongPress = true;
  root.rebuild();
  h.frame();
  CHECK_EQ(regionOf(h).recognizerCount(), std::size_t{2});
}

TEST(components_a_button_in_a_tree_with_no_focus_scope_is_pointer_only) {
  Harness h;
  FocusNode node;
  int presses = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [&presses] { ++presses; },
                                    .focusNode = &node,
                                    .autofocus = true,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  CHECK_EQ(h.binding().keyboard().handlerCount(), std::size_t{0});
  CHECK(!node.attached());
  CHECK(dumpElementTree(h.rootElement()).find("FocusMarker") == std::string::npos);
  CHECK(!h.binding().dispatchKey(down(LogicalKey::Space)));

  // And it is a perfectly good button.
  tap(h);
  CHECK_EQ(presses, 1);
}

TEST(components_a_frame_of_hover_and_press_allocates_nothing) {
  Harness h;
  WidgetStatesController states;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawButton::make({
                                    .onPressed = [] {},
                                    .statesController = &states,
                                    .child = fill(),
                                }))});
  });
  h.frame();
  for (int i = 0; i < 4; ++i) {
    h.binding().dispatchPointer(mouse(PointerPhase::Hover, kCentre));
    tap(h);
    h.binding().dispatchPointer(mouse(PointerPhase::Hover, kAway));
    h.frame();
  }

  const std::size_t before = allocationCount();
  for (int i = 0; i < 8; ++i) {
    h.binding().dispatchPointer(mouse(PointerPhase::Hover, kCentre));
    tap(h);
    h.binding().dispatchPointer(mouse(PointerPhase::Hover, kAway));
    h.frame();
  }
  CHECK_EQ(allocationCount() - before, std::size_t{0});
}

// ---------------------------------------------------------------------------
// RawToggle
// ---------------------------------------------------------------------------

TEST(components_a_toggle_reports_where_it_would_go_and_assumes_nothing) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  ToggleValue reported = ToggleValue::Off;
  int reports = 0;
  ValueListenable<float>* position = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .onChanged =
                                        [&](ToggleValue next) {
                                          reported = next;
                                          ++reports;
                                        },
                                    .child = Probe::make({.fraction = &position, .child = fill()}),
                                }))});
  });
  h.frame();

  tap(h);
  CHECK_EQ(reports, 1);
  CHECK(reported == ToggleValue::On);
  // Nothing moved: the value is the consumer's, and the report was only a
  // report.
  CHECK_EQ(position->value(), 0.0f);

  value = ToggleValue::On;
  root.rebuild();
  h.frame();
  h.frame(0.2f);
  CHECK_EQ(position->value(), 1.0f);

  tap(h);
  CHECK_EQ(reports, 2);
  CHECK(reported == ToggleValue::Off);
}

TEST(components_a_toggle_animates_to_the_value_rather_than_jumping) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  ValueListenable<float>* position = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .positionDuration = 0.2f,
                                    .child = Probe::make({.fraction = &position, .child = fill()}),
                                }))});
  });
  h.frame();
  CHECK_EQ(position->value(), 0.0f);

  value = ToggleValue::On;
  root.rebuild();
  h.frame();
  CHECK_EQ(position->value(), 0.0f);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  h.frame(0.1f);
  CHECK(position->value() > 0.0f);
  CHECK(position->value() < 1.0f);

  h.frame(0.15f);
  CHECK_EQ(position->value(), 1.0f);
  // Settled, and off the clock: a screen of toggles at rest runs nothing.
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
}

TEST(components_a_tristate_toggle_cycles_through_mixed) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  ValueListenable<float>* position = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .onChanged = [&](ToggleValue next) { value = next; },
                                    .tristate = true,
                                    .child = Probe::make({.fraction = &position, .child = fill()}),
                                }))});
  });
  h.frame();

  tap(h);
  CHECK(value == ToggleValue::On);
  root.rebuild();
  h.frame();

  tap(h);
  CHECK(value == ToggleValue::Mixed);
  root.rebuild();
  h.frame();
  h.frame(0.2f);
  CHECK_EQ(position->value(), 0.5f);

  tap(h);
  CHECK(value == ToggleValue::Off);
}

TEST(components_a_radio_is_a_toggle_its_own_press_cannot_clear) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  int reports = 0;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .onChanged =
                                        [&](ToggleValue next) {
                                          value = next;
                                          ++reports;
                                        },
                                    .canToggleOff = false,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  tap(h);
  CHECK(value == ToggleValue::On);
  CHECK_EQ(reports, 1);

  root.rebuild();
  h.frame();
  tap(h);
  CHECK(value == ToggleValue::On);
  CHECK_EQ(reports, 1);
}

/// A switch travels about as far as a finger's slop, so what it does over the
/// first few pixels of a mouse drag is the whole feel of it: holding a mouse to
/// the finger's figure would leave the thumb still until the pointer had passed
/// the far end, and then throw it the whole way at once.
TEST(components_a_switch_follows_a_mouse_from_the_first_pixels) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  ValueListenable<float>* position = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .onChanged = [](ToggleValue) {},
                                    .dragExtent = 40.0f,
                                    .child = Probe::make({.fraction = &position, .child = fill()}),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {30, 30}));
  // Two pixels is already a drag for a mouse, and what it cost to be recognized
  // is what the thumb stays behind by -- not what it jumps by.
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {32, 30}));
  CHECK_EQ(position->value(), 0.0f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {42, 30}));
  CHECK_EQ(position->value(), 0.25f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {52, 30}));
  CHECK_EQ(position->value(), 0.5f);
}

TEST(components_a_switch_settles_to_the_side_the_drag_left_it_on) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  ToggleValue reported = ToggleValue::Off;
  WidgetStatesController states;
  ValueListenable<float>* position = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .onChanged = [&](ToggleValue next) { reported = next; },
                                    .dragExtent = 40.0f,
                                    .statesController = &states,
                                    .child = Probe::make({.fraction = &position, .child = fill()}),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {30, 30}));
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {60, 30}));
  // One move this large is past the tap's slop as well, so the tap gives it up
  // and the arena has awarded the drag before the drag itself sees the event:
  // there is no travel left over for the start behaviour to swallow, and the
  // thumb lands under the finger.
  CHECK(states.has(WidgetState::Dragged));
  CHECK_EQ(position->value(), 0.75f);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, {60, 30}));
  CHECK(!states.has(WidgetState::Dragged));
  CHECK(reported == ToggleValue::On);
}

TEST(components_a_drag_that_stops_short_reports_nothing_and_returns_the_thumb) {
  Harness h;
  ToggleValue value = ToggleValue::Off;
  int reports = 0;
  ValueListenable<float>* position = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({
                                    .value = value,
                                    .onChanged = [&reports](ToggleValue) { ++reports; },
                                    .dragExtent = 100.0f,
                                    .child = Probe::make({.fraction = &position, .child = fill()}),
                                }))});
  });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Down, {30, 30}));
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {55, 30}));
  CHECK_EQ(position->value(), 0.25f);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, {55, 30}));
  CHECK_EQ(reports, 0);

  // A build is asked for whether or not anything accepted a report, which is
  // what brings the thumb back to where the value actually is.
  h.frame();
  h.frame(0.2f);
  CHECK_EQ(position->value(), 0.0f);
}

TEST(components_a_checkbox_creates_no_drag_recognizer) {
  Harness h;
  float dragExtent = 0.0f;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawToggle::make({.dragExtent = dragExtent, .child = fill()}))});
  });
  h.frame();
  CHECK_EQ(regionOf(h).recognizerCount(), std::size_t{1});

  dragExtent = 40.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(regionOf(h).recognizerCount(), std::size_t{2});
}

TEST(components_a_toggle_activates_from_the_keyboard) {
  Harness h;
  FocusNode node;
  ToggleValue value = ToggleValue::Off;
  ScriptedRoot root(h, [&] {
    return FocusScope::make(
        {.child = screen({placed(kBox, RawToggle::make({
                                           .value = value,
                                           .onChanged = [&](ToggleValue next) { value = next; },
                                           .focusNode = &node,
                                           .autofocus = true,
                                           .child = fill(),
                                       }))})});
  });
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::Space)));
  CHECK(value == ToggleValue::Off);
  CHECK(h.binding().dispatchKey(up(LogicalKey::Space)));
  CHECK(value == ToggleValue::On);
}

// ---------------------------------------------------------------------------
// RawSlider
// ---------------------------------------------------------------------------

TEST(components_a_slider_maps_the_pointer_across_its_track) {
  Harness h;
  float value = 0.0f;
  ValueListenable<float>* fraction = nullptr;
  WidgetStatesController states;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawSlider::make({
                                    .value = value,
                                    .onChanged = [&](float next) { value = next; },
                                    .statesController = &states,
                                    .child = Probe::make({.fraction = &fraction, .child = fill()}),
                                }))});
  });
  h.frame();

  // The box runs from x=20 to x=80, so a press fifteen pixels in is a quarter.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {35, 30}));
  CHECK_EQ(value, 0.25f);
  CHECK(states.has(WidgetState::Pressed));

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {65, 30}));
  CHECK_EQ(value, 0.75f);
  CHECK(states.has(WidgetState::Dragged));

  // Past the end is the end.
  h.binding().dispatchPointer(mouse(PointerPhase::Move, {200, 30}));
  CHECK_EQ(value, 1.0f);

  h.binding().dispatchPointer(mouse(PointerPhase::Up, {200, 30}));
  CHECK(!states.has(WidgetState::Dragged));
  CHECK(!states.has(WidgetState::Pressed));

  root.rebuild();
  h.frame();
  CHECK_EQ(fraction->value(), 1.0f);
}

TEST(components_a_slider_with_divisions_snaps_to_them) {
  Harness h;
  float value = 0.0f;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawSlider::make({
                                    .value = value,
                                    .min = 0.0f,
                                    .max = 100.0f,
                                    .divisions = 4,
                                    .onChanged = [&](float next) { value = next; },
                                    .child = fill(),
                                }))});
  });
  h.frame();

  // A third of the way along lands on the stop at a quarter.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {40, 30}));
  CHECK_EQ(value, 25.0f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {53, 30}));
  CHECK_EQ(value, 50.0f);
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {53, 30}));
}

TEST(components_a_slider_keeps_the_thumb_inside_the_box) {
  Harness h;
  float value = 0.0f;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawSlider::make({
                                    .value = value,
                                    .onChanged = [&](float next) { value = next; },
                                    .thumbExtent = 20.0f,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  // Sixty wide less a twenty-wide thumb leaves forty of travel, starting ten in.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {30, 30}));
  CHECK_EQ(value, 0.0f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {50, 30}));
  CHECK_EQ(value, 0.5f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {70, 30}));
  CHECK_EQ(value, 1.0f);
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {70, 30}));
}

TEST(components_a_slider_steps_from_the_keyboard_and_leaves_the_cross_axis_alone) {
  Harness h;
  FocusNode node;
  FocusNode above;
  float value = 0.5f;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .child = screen({
            placed(Rect::fromLTWH(20, 60, 60, 30),
                   RawSlider::make({
                       .value = value,
                       .onChanged = [&](float next) { value = next; },
                       .focusNode = &node,
                       .autofocus = true,
                       .child = SizedBox::make({.size = {60, 30}}),
                   })),
            placed(Rect::fromLTWH(20, 10, 60, 30),
                   Focus::make({.node = &above, .child = SizedBox::make({.size = {60, 30}})})),
        }),
    });
  });
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::ArrowRight)));
  CHECK_EQ(value, 0.6f);
  root.rebuild();
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::ArrowLeft)));
  CHECK_EQ(value, 0.5f);
  root.rebuild();
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::Home)));
  CHECK_EQ(value, 0.0f);
  root.rebuild();
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::End)));
  CHECK_EQ(value, 1.0f);
  root.rebuild();
  h.frame();

  // The axis it does not run along is traversal's, which is what stops a D-pad
  // getting stuck in a slider.
  CHECK(h.binding().dispatchKey(down(LogicalKey::ArrowUp)));
  CHECK(above.hasPrimaryFocus());
  CHECK_EQ(value, 1.0f);
}

TEST(components_a_vertical_slider_runs_the_way_its_coordinates_do) {
  Harness h;
  FocusNode node;
  FocusNode beside;
  float value = 0.0f;
  constexpr Rect kTall = Rect::fromLTWH(20, 10, 30, 60);
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .child = screen({
            placed(kTall, RawSlider::make({
                              .value = value,
                              .onChanged = [&](float next) { value = next; },
                              .axis = Axis::Vertical,
                              .keyStep = 0.25f,
                              .focusNode = &node,
                              .autofocus = true,
                              .child = SizedBox::make({.size = kTall.size()}),
                          })),
            placed(Rect::fromLTWH(100, 10, 30, 60),
                   Focus::make({.node = &beside, .child = SizedBox::make({.size = {30, 60}})})),
        }),
    });
  });
  h.frame();

  // Sixty tall, so fifteen pixels down is a quarter -- and further down is
  // further up the range, because the fraction runs with the coordinate space.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {35, 25}));
  CHECK_EQ(value, 0.25f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {35, 55}));
  CHECK_EQ(value, 0.75f);
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {35, 55}));
  root.rebuild();
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::ArrowUp)));
  CHECK_EQ(value, 0.5f);
  root.rebuild();
  h.frame();

  CHECK(h.binding().dispatchKey(down(LogicalKey::ArrowDown)));
  CHECK_EQ(value, 0.75f);
  root.rebuild();
  h.frame();

  // Across the axis is traversal's, whichever axis this one runs along.
  CHECK(h.binding().dispatchKey(down(LogicalKey::ArrowRight)));
  CHECK(beside.hasPrimaryFocus());
  CHECK_EQ(value, 0.75f);
}

TEST(components_a_vertical_slider_keeps_the_thumb_inside_the_box) {
  Harness h;
  float value = 0.0f;
  constexpr Rect kTall = Rect::fromLTWH(20, 10, 30, 60);
  ScriptedRoot root(h, [&] {
    return screen({placed(kTall, RawSlider::make({
                                     .value = value,
                                     .onChanged = [&](float next) { value = next; },
                                     .axis = Axis::Vertical,
                                     .thumbExtent = 20.0f,
                                     .child = SizedBox::make({.size = kTall.size()}),
                                 }))});
  });
  h.frame();

  // Sixty tall less a twenty-tall thumb leaves forty of travel, starting ten in.
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {35, 20}));
  CHECK_EQ(value, 0.0f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {35, 40}));
  CHECK_EQ(value, 0.5f);

  h.binding().dispatchPointer(mouse(PointerPhase::Move, {35, 60}));
  CHECK_EQ(value, 1.0f);
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {35, 60}));
}

TEST(components_a_disabled_slider_reports_nothing) {
  Harness h;
  float value = 0.5f;
  WidgetStatesController states;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawSlider::make({
                                    .value = value,
                                    .onChanged = [&](float next) { value = next; },
                                    .enabled = false,
                                    .statesController = &states,
                                    .child = fill(),
                                }))});
  });
  h.frame();

  CHECK(states.has(WidgetState::Disabled));
  h.binding().dispatchPointer(mouse(PointerPhase::Down, {35, 30}));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, {35, 30}));
  CHECK_EQ(value, 0.5f);
}

// ---------------------------------------------------------------------------
// RawProgress
// ---------------------------------------------------------------------------

TEST(components_a_determinate_progress_publishes_a_fraction_and_holds_no_ticker) {
  Harness h;
  float value = 30.0f;
  ValueListenable<float>* fraction = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawProgress::make({
                                    .value = value,
                                    .min = 0.0f,
                                    .max = 120.0f,
                                    .child = Probe::make({.fraction = &fraction, .child = fill()}),
                                }))});
  });
  h.frame();

  CHECK_EQ(fraction->value(), 0.25f);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});

  value = 240.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(fraction->value(), 1.0f);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
}

TEST(components_an_indeterminate_progress_sweeps_and_wraps) {
  Harness h;
  bool indeterminate = false;
  ValueListenable<float>* fraction = nullptr;
  ScriptedRoot root(h, [&] {
    return screen({placed(kBox, RawProgress::make({
                                    .value = 0.7f,
                                    .indeterminate = indeterminate,
                                    .period = 1.0f,
                                    .child = Probe::make({.fraction = &fraction, .child = fill()}),
                                }))});
  });
  h.frame();
  CHECK_EQ(fraction->value(), 0.7f);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});

  indeterminate = true;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});
  // The sweep starts where the driver is, not where the old fill was: a bar
  // showing 0.7 for one more frame would be showing a number that means nothing.
  CHECK_EQ(fraction->value(), 0.0f);

  h.frame(0.25f);
  CHECK_EQ(fraction->value(), 0.25f);
  h.frame(0.5f);
  CHECK_EQ(fraction->value(), 0.75f);

  // Round the end and start again, rather than stopping there.
  h.frame(0.5f);
  CHECK_EQ(fraction->value(), 0.25f);

  indeterminate = false;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
  CHECK_EQ(fraction->value(), 0.7f);
}
