#include "testing.hpp"

#include <memory>
#include <string>

#include "fltr/harness.hpp"
#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/focus.hpp"
#include "fltr/widgets/switcher.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

constexpr Size kSurface{200, 100};
constexpr Size kPanel{60, 40};
constexpr Offset kCentre{100, 50};

PointerEvent mouse(PointerPhase phase, Offset position) {
  return {phase, 0, PointerDeviceKind::Mouse, position};
}

void tap(Harness& h, Offset at = kCentre) {
  h.binding().dispatchPointer(mouse(PointerPhase::Down, at));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, at));
}

/// Everything a test needs to know about one panel: whether it is still there,
/// whether it is still building, whether time still reaches it, and whether the
/// pointer does.
struct Life {
  int inits = 0;
  int disposes = 0;
  int builds = 0;
  int taps = 0;
  float elapsed = 0.0f;
};

class Panel;

class PanelState final : public State<Panel> {
public:
  void initState() override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

private:
  static void advance(void* self, float seconds);

  Ticker ticker_{&PanelState::advance, this};
};

/// A subtree with a State, a ticker, a focus node and a hit region -- the four
/// things a retained subtree is supposed to keep.
class Panel final : public Configure<Panel, StatefulWidget> {
public:
  struct Args {
    Key key;
    Life* life = nullptr;
    FocusNode* focusNode = nullptr;
    bool autofocus = false;
    /// Off by default, so a test that is not about time is not kept awake.
    bool ticking = false;
    Color color = Color::argb(0xFF203040);
  };

  explicit Panel(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Panel"; }
  const Args& args() const noexcept { return args_; }
  Life& life() const noexcept { return *args_.life; }

  std::unique_ptr<State<Panel>> createState() const { return std::make_unique<PanelState>(); }

private:
  Args args_;
};

void PanelState::initState() {
  ++widget().life().inits;
  if (!widget().args().ticking) return;
  ticker_.attach(context().tickers());
  ticker_.start();
}

void PanelState::dispose() {
  ++widget().life().disposes;
  ticker_.stop();
  ticker_.detach();
}

void PanelState::advance(void* self, float seconds) {
  static_cast<PanelState*>(self)->widget().life().elapsed += seconds;
}

WidgetRef PanelState::build(BuildContext&) {
  Life* life = &widget().life();
  ++life->builds;
  return Focus::make({
      .node = widget().args().focusNode,
      .autofocus = widget().args().autofocus,
      .child = Pointer::make({
          .behavior = HitTestBehavior::Opaque,
          .onTap = [life] { ++life->taps; },
          .child = SizedBox::make({
              .size = kPanel,
              .child = DecoratedBox::make({.decoration = {.color = widget().args().color}}),
          }),
      }),
  });
}

AnimatedSwitcherState& switcherOf(Harness& h) {
  auto& element = static_cast<StatefulElement<AnimatedSwitcher>&>(
      elementFor(h.rootElement(), widgetTypeOf<AnimatedSwitcher>()));
  return static_cast<AnimatedSwitcherState&>(element.state());
}

/// How present the nth subtree the switcher has shown currently is. Each one is
/// wrapped in a keyed guard, which is what makes it nameable from here.
float presenceOf(Harness& h, int serial) {
  Element& guard = elementFor(h.rootElement(), widgetTypeOf<Focus>(), Key::of(serial));
  return static_cast<RenderOpacity&>(*elementFor(guard, widgetTypeOf<Opacity>()).renderObject())
      .opacity();
}

bool holds(Harness& h, int serial) {
  return dumpElementTree(h.rootElement()).find("key=" + std::to_string(serial)) !=
         std::string::npos;
}

}  // namespace

// ---------------------------------------------------------------------------
// Retention
// ---------------------------------------------------------------------------

TEST(switcher_shows_its_first_child_without_animating_it_in) {
  Harness h(kSurface);
  Life first;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = Panel::make({.life = &first}),
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();

  CHECK_EQ(first.inits, 1);
  CHECK_EQ(presenceOf(h, 1), 1.0f);
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{1});
  // A panel does not fade in because the screen it sits on appeared.
  CHECK(!h.binding().needsFrame());
}

TEST(switcher_retains_the_outgoing_subtree_until_its_animation_finishes) {
  Harness h(kSurface);
  Life first;
  Life second;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = swapped ? Panel::make({.key = Key::of("second"), .life = &second})
                         : Panel::make({.key = Key::of("first"), .life = &first}),
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();

  swapped = true;
  root.rebuild();
  h.frame();

  // Both are mounted, and the one leaving is exactly where it was.
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{2});
  CHECK_EQ(first.disposes, 0);
  CHECK_EQ(second.inits, 1);
  CHECK_EQ(presenceOf(h, 1), 1.0f);
  CHECK_EQ(presenceOf(h, 2), 0.0f);

  h.frame(0.1f);
  CHECK_NEAR(presenceOf(h, 1), 0.5f, 1e-5);
  CHECK_NEAR(presenceOf(h, 2), 0.5f, 1e-5);
  CHECK_EQ(first.disposes, 0);

  h.frame(0.1f);
  CHECK_EQ(first.disposes, 1);
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{1});
  CHECK(!holds(h, 1));
  CHECK_EQ(presenceOf(h, 2), 1.0f);
}

TEST(switcher_the_outgoing_subtree_keeps_ticking) {
  Harness h(kSurface);
  Life first;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = swapped ? WidgetRef{}
                         : Panel::make({.key = Key::of("first"), .life = &first, .ticking = true}),
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();

  swapped = true;
  root.rebuild();
  h.frame();

  // Still mounted and still ticking: the whole point of the milestone, as a
  // number the subtree accumulated on its own after it was removed.
  const float before = first.elapsed;
  h.frame(0.1f);
  CHECK_NEAR(first.elapsed - before, 0.1f, 1e-5);
  CHECK_EQ(first.disposes, 0);

  h.frame(0.1f);
  CHECK_EQ(first.disposes, 1);
  const float settled = first.elapsed;
  h.frame(0.1f);
  CHECK_EQ(first.elapsed, settled);
}

TEST(switcher_the_outgoing_subtree_is_retained_rather_than_rebuilt) {
  Harness h(kSurface);
  Life first;
  Life second;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = swapped ? Panel::make({.key = Key::of("second"), .life = &second})
                         : Panel::make({.key = Key::of("first"), .life = &first}),
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();
  CHECK_EQ(first.builds, 1);

  swapped = true;
  root.rebuild();
  h.frame();
  h.frame(0.05f);
  h.frame(0.05f);

  // The ref naming it is stale from the build that removed it onward, so every
  // frame of the exit leaves the subtree entirely alone: no rebuild, no
  // reconciliation, and the same State throughout.
  CHECK_EQ(first.builds, 1);
  CHECK_EQ(first.inits, 1);
  CHECK_EQ(h.buildOwner().buildCount(), 0);
}

TEST(switcher_updating_the_same_child_is_not_a_swap) {
  Harness h(kSurface);
  Life only;
  Color color = Color::argb(0xFF203040);

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = Panel::make({.key = Key::of("only"), .life = &only, .color = color}),
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();

  color = Color::argb(0xFFFF0000);
  root.rebuild();
  h.frame();

  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{1});
  CHECK_EQ(only.inits, 1);
  CHECK_EQ(only.builds, 2);
  CHECK_EQ(presenceOf(h, 1), 1.0f);
  CHECK(!h.binding().needsFrame());
}

TEST(switcher_a_null_child_animates_the_last_one_out) {
  Harness h(kSurface);
  Life only;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = visible ? Panel::make({.life = &only}) : WidgetRef{},
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();

  visible = false;
  root.rebuild();
  h.frame();
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{1});
  CHECK_EQ(only.disposes, 0);

  h.frame(0.2f);
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{0});
  CHECK_EQ(only.disposes, 1);
  CHECK(!h.binding().needsFrame());
}

TEST(switcher_coming_back_re_enters_the_subtree_still_leaving) {
  Harness h(kSurface);
  Life only;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = visible ? Panel::make({.life = &only}) : WidgetRef{},
        .animation = {.duration = 0.2f},
    });
  });
  h.frame();

  visible = false;
  root.rebuild();
  h.frame();
  h.frame(0.1f);
  CHECK_NEAR(presenceOf(h, 1), 0.5f, 1e-5);

  // Back again, halfway out. It continues from where it had faded to rather
  // than starting a second subtree beside the first.
  visible = true;
  root.rebuild();
  h.frame();
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{1});
  CHECK_EQ(only.inits, 1);
  CHECK_EQ(only.disposes, 0);
  CHECK_NEAR(presenceOf(h, 1), 0.5f, 1e-5);

  h.frame(0.1f);
  CHECK_EQ(presenceOf(h, 1), 1.0f);
  CHECK(!h.binding().needsFrame());
}

// ---------------------------------------------------------------------------
// A subtree on its way out is inert
// ---------------------------------------------------------------------------

TEST(switcher_a_subtree_on_its_way_out_takes_no_pointer) {
  Harness h(kSurface);
  Life only;
  int behind = 0;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return Stack::make({
        .fit = StackFit::Expand,
        .children = {Pointer::make({
                         .behavior = HitTestBehavior::Opaque,
                         .onTap = [&behind] { ++behind; },
                     }),
                     AnimatedSwitcher::make({
                         .child = visible ? Panel::make({.life = &only}) : WidgetRef{},
                         .animation = {.duration = 0.2f},
                     })},
    });
  });
  h.frame();

  tap(h);
  CHECK_EQ(only.taps, 1);
  CHECK_EQ(behind, 0);

  visible = false;
  root.rebuild();
  h.frame();

  // Fully visible still, and already unreachable: what is leaving is gone as
  // far as input is concerned.
  CHECK_EQ(presenceOf(h, 1), 1.0f);
  tap(h);
  CHECK_EQ(only.taps, 1);
  CHECK_EQ(behind, 1);
}

TEST(switcher_a_subtree_on_its_way_out_cannot_hold_the_focus) {
  Harness h(kSurface);
  Life only;
  FocusScopeNode scope;
  FocusNode node;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = AnimatedSwitcher::make({
            .child = visible ? Panel::make({.life = &only, .focusNode = &node, .autofocus = true})
                             : WidgetRef{},
            .animation = {.duration = 0.2f},
        }),
    });
  });
  h.frame();
  CHECK(node.hasPrimaryFocus());

  visible = false;
  root.rebuild();
  h.frame();

  // The keys go somewhere else the instant it starts leaving, rather than into
  // a control the player can no longer see the point of.
  CHECK(!node.hasPrimaryFocus());
  CHECK(!node.isFocusable());
  CHECK_EQ(only.disposes, 0);
}

// ---------------------------------------------------------------------------
// What it costs, and who decides what it looks like
// ---------------------------------------------------------------------------

TEST(switcher_the_transition_belongs_to_the_call_site) {
  Harness h(kSurface);
  Life first;
  Life second;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = swapped ? Panel::make({.key = Key::of("second"), .life = &second})
                         : Panel::make({.key = Key::of("first"), .life = &first}),
        .animation = {.duration = 0.2f},
        .transition = [](WidgetRef child, ValueListenable<float>& presence) {
          return ClipRect::make({.child = Opacity::make({.animation = &presence, .child = child})});
        },
    });
  });
  h.frame();
  CHECK(dumpElementTree(h.rootElement()).find("ClipRect") != std::string::npos);

  swapped = true;
  root.rebuild();
  h.frame();
  h.frame(0.1f);
  CHECK_NEAR(presenceOf(h, 1), 0.5f, 1e-5);
  CHECK_NEAR(presenceOf(h, 2), 0.5f, 1e-5);
  CHECK(dumpElementTree(h.rootElement()).find("ClipRect") != std::string::npos);
}

TEST(switcher_a_muted_switcher_holds_still) {
  Harness h(kSurface);
  Life first;
  Life second;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    return TickerMode::make({
        .enabled = false,
        .child = AnimatedSwitcher::make({
            .child = swapped ? Panel::make({.key = Key::of("second"), .life = &second})
                             : Panel::make({.key = Key::of("first"), .life = &first}),
            .animation = {.duration = 0.2f},
        }),
    });
  });
  h.frame();

  swapped = true;
  root.rebuild();
  h.frame();
  h.frame(1.0f);

  // Nothing hidden costs time, so the subtree that was leaving is still there,
  // still where it was, waiting to be shown again.
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{2});
  CHECK_EQ(presenceOf(h, 1), 1.0f);
  CHECK_EQ(presenceOf(h, 2), 0.0f);
  CHECK_EQ(first.disposes, 0);
}

TEST(switcher_a_frame_of_exit_animation_allocates_nothing) {
  Harness h(kSurface);
  Life first;
  Life second;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    return AnimatedSwitcher::make({
        .child = swapped ? Panel::make({.key = Key::of("second"), .life = &second})
                         : Panel::make({.key = Key::of("first"), .life = &first}),
        .animation = {.duration = 1.0f},
    });
  });
  h.frame();

  swapped = true;
  root.rebuild();
  h.frame();
  h.frame(0.05f);

  const std::size_t before = allocationCount();
  for (int i = 0; i < 8; ++i) h.frame(0.05f);
  CHECK_EQ(allocationCount() - before, std::size_t{0});
  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(switcherOf(h).liveCount(), std::size_t{2});
}
