#include "testing.hpp"

#include <memory>

#include "fltr/harness.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/reactive.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

/// Ambient data of the shape a HUD actually carries: a colour and a scale, both
/// trivially destructible, compared by value.
struct Theme {
  Color color = Color::argb(0xFF102030);
  float scale = 10.0f;

  bool operator==(const Theme&) const = default;
};

/// What one widget's builds are worth knowing about: how many ran, and what the
/// last one saw.
struct Reads {
  int builds = 0;
  Theme theme;
  int value = 0;
};

/// Reads the ambient theme, or deliberately does not, so an element that stops
/// reading can be observed stopping.
class ThemeReader final : public Configure<ThemeReader, StatelessWidget> {
public:
  struct Args {
    Key key;
    Reads* into = nullptr;
    bool reads = true;
  };

  explicit ThemeReader(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "ThemeReader"; }

  WidgetRef build(BuildContext& context) const {
    ++args_.into->builds;
    if (!args_.reads) return SizedBox::make({.size = {5, 5}});
    args_.into->theme = Ambient<Theme>::of(context);
    return SizedBox::make({.size = {args_.into->theme.scale, args_.into->theme.scale}});
  }

private:
  Args args_;
};

/// A leaf that records the value it was built with, so "which subtree rebuilt"
/// is answered by counters rather than by inspection.
class Report final : public Configure<Report, StatelessWidget> {
public:
  struct Args {
    Key key;
    Reads* into = nullptr;
    int value = 0;
  };

  explicit Report(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Report"; }

  WidgetRef build(BuildContext&) const {
    ++args_.into->builds;
    args_.into->value = args_.value;
    return SizedBox::make({.size = {5, 5}});
  }

private:
  Args args_;
};

class Themed;

/// Publishes a theme it can change on its own, re-emitting the child it was
/// configured with. That child ref belongs to an earlier build, so the subtree
/// below the Ambient is skipped wholesale on every theme change -- which is what
/// makes "only the readers rebuilt" a real claim rather than an accident of
/// where the rebuild started.
class ThemedState final : public State<Themed> {
public:
  void initState() override;
  WidgetRef build(BuildContext& context) override;

  void set(Theme next) {
    setState([&] { theme_ = next; });
  }

private:
  Theme theme_;
};

class Themed final : public Configure<Themed, StatefulWidget> {
public:
  struct Args {
    Key key;
    Theme initial;
    WidgetRef child;
  };

  explicit Themed(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Themed"; }
  Theme initial() const noexcept { return args_.initial; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Themed>> createState() const { return std::make_unique<ThemedState>(); }

private:
  Args args_;
};

void ThemedState::initState() { theme_ = widget().initial(); }

WidgetRef ThemedState::build(BuildContext&) {
  return Ambient<Theme>::make({.value = theme_, .child = widget().child()});
}

class Closing;

/// Reports its own closing by pushing a value, the way a panel telling the game
/// it went away does. It runs while the subtree around it is still being torn
/// down, which is what makes a sibling's teardown order observable.
class ClosingState final : public State<Closing> {
public:
  WidgetRef build(BuildContext&) override { return SizedBox::make({.size = {5, 5}}); }
  void dispose() override;
};

class Closing final : public Configure<Closing, StatefulWidget> {
public:
  struct Args {
    Key key;
    Observable<int>* value = nullptr;
  };

  explicit Closing(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Closing"; }
  Observable<int>& observable() const noexcept { return *args_.value; }

  std::unique_ptr<State<Closing>> createState() const { return std::make_unique<ClosingState>(); }

private:
  Args args_;
};

void ClosingState::dispose() { widget().observable().set(-1); }

/// The other way to consume an observable: the value goes straight to the render
/// object, which repaints itself and nothing more. No element takes part, so a
/// push costs no build and no layout.
class RenderTint final : public RenderBox {
public:
  const char* typeName() const override { return "Tint"; }

  void setTint(Observable<Color>* tint) {
    if (tint == tint_) return;
    tint_ = tint;
    observeForPaint(subscription_, tint);
    markNeedsPaint();
  }

  void performLayout() override { setSize(constraints_.constrain({10, 10})); }

  void paint(PaintingContext& context, Offset offset) override {
    context.list().drawRect(Rect::fromOriginSize(offset, size_), tint_->value());
  }

private:
  Observable<Color>* tint_ = nullptr;
  Subscription subscription_;
};

class Tint final : public Configure<Tint, LeafRenderObjectWidget> {
public:
  struct Args {
    Key key;
    Observable<Color>* tint = nullptr;
  };
  using Render = RenderTint;

  explicit Tint(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Tint"; }

  std::unique_ptr<RenderTint> createRenderObject(BuildContext&) const {
    auto render = std::make_unique<RenderTint>();
    render->setTint(args_.tint);
    return render;
  }
  void updateRenderObject(BuildContext&, RenderTint& render) const { render.setTint(args_.tint); }

private:
  Args args_;
};

WidgetRef watched(Observable<int>& value, Reads& into) {
  return Watch<int>::make({
      .value = &value,
      .builder = [&into](BuildContext&, const int& v) {
        return Report::make({.into = &into, .value = v});
      },
  });
}

InheritedElement<Ambient<Theme>>& themeElement(Element& root) {
  return static_cast<InheritedElement<Ambient<Theme>>&>(
      elementFor(root, widgetTypeOf<Ambient<Theme>>()));
}

ThemedState& themedState(Element& root) {
  return static_cast<ThemedState&>(
      static_cast<StatefulElement<Themed>&>(elementFor(root, widgetTypeOf<Themed>())).state());
}

}  // namespace

// ---------------------------------------------------------------------------
// Pushed values
// ---------------------------------------------------------------------------

TEST(reactivity_pushing_unchanged_values_does_no_work) {
  Harness h;
  Observable<int> shield{100};
  Observable<int> ammo{30};
  Reads shieldReads, ammoReads;

  h.attach([&] {
    return Column::make({.children = {watched(shield, shieldReads), watched(ammo, ammoReads)}});
  });
  h.frame();
  const Scene first = h.frame();
  h.buildOwner().resetBuildCount();

  // The game loop's whole per-frame push, with nothing actually changed.
  for (int i = 0; i < 3; ++i) {
    shield.set(100);
    ammo.set(30);
    CHECK(!h.binding().needsFrame());
    CHECK_EQ(h.frame().revision, first.revision);
  }

  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().paints, 0);
  CHECK_EQ(shieldReads.builds, 1);
  CHECK_EQ(ammoReads.builds, 1);
}

TEST(reactivity_a_changed_value_rebuilds_only_the_subtree_that_reads_it) {
  Harness h;
  Observable<int> shield{100};
  Observable<int> ammo{30};
  Reads shieldReads, ammoReads;

  h.attach([&] {
    return Column::make({.children = {watched(shield, shieldReads), watched(ammo, ammoReads)}});
  });
  h.frame();
  h.buildOwner().resetBuildCount();

  shield.set(75);
  CHECK(h.binding().needsFrame());
  h.frame();

  // One dirty element -- the Watch -- and its own subtree beneath it.
  CHECK_EQ(h.buildOwner().buildCount(), 1);
  CHECK_EQ(shieldReads.builds, 2);
  CHECK_EQ(shieldReads.value, 75);
  CHECK_EQ(ammoReads.builds, 1);
}

TEST(reactivity_pushes_between_frames_coalesce_into_one_build) {
  Harness h;
  Observable<int> shield{100};
  Reads reads;

  h.attach([&] { return watched(shield, reads); });
  h.frame();

  shield.set(90);
  shield.set(80);
  shield.set(70);
  h.frame();

  CHECK_EQ(reads.builds, 2);
  CHECK_EQ(reads.value, 70);
}

TEST(reactivity_a_watch_that_leaves_the_tree_holds_no_subscription) {
  Harness h;
  Observable<int> shield{100};
  Reads reads;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {visible ? watched(shield, reads) : WidgetRef{}}});
  });
  h.frame();
  CHECK_EQ(shield.listenerCount(), std::size_t{1});

  visible = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(shield.listenerCount(), std::size_t{0});
  shield.set(10);
  CHECK(!h.binding().needsFrame());
  CHECK_EQ(reads.builds, 1);
}

TEST(reactivity_a_watch_stops_listening_when_it_unmounts_not_when_it_is_destroyed) {
  Harness h;
  Observable<int> shield{100};
  Reads reads;
  bool visible = true;

  // Removing the Column unmounts both children and destroys neither until the
  // whole subtree is gone, so the sibling's dispose runs while this Watch is
  // unmounted but still alive. A subscription that outlived mounting would
  // rebuild an element that no longer has a tree.
  ScriptedRoot root(h, [&] {
    return visible ? Column::make({.children = {
                                       watched(shield, reads),
                                       Closing::make({.value = &shield}),
                                   }})
                   : SizedBox::make({.size = {5, 5}});
  });
  h.frame();

  visible = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(shield.listenerCount(), std::size_t{0});
  CHECK_EQ(shield.value(), -1);
  CHECK_EQ(reads.builds, 1);
}

TEST(reactivity_a_watch_retargeted_at_another_value_follows_it) {
  Harness h;
  Observable<int> shield{100};
  Observable<int> armour{50};
  Reads reads;
  Observable<int>* watching = &shield;

  ScriptedRoot root(h, [&] { return watched(*watching, reads); });
  h.frame();

  watching = &armour;
  root.rebuild();
  h.frame();

  CHECK_EQ(shield.listenerCount(), std::size_t{0});
  CHECK_EQ(armour.listenerCount(), std::size_t{1});

  shield.set(1);
  CHECK(!h.binding().needsFrame());

  armour.set(25);
  h.frame();
  CHECK_EQ(reads.value, 25);
}

TEST(reactivity_a_value_consumed_by_a_render_object_repaints_without_rebuilding) {
  Harness h;
  Observable<Color> tint{Color::argb(0xFF010203)};

  h.attach([&] { return Tint::make({.tint = &tint}); });
  h.frame();
  const Scene before = h.frame();
  h.buildOwner().resetBuildCount();

  tint.set(Color::argb(0xFF807060));
  const Scene after = h.frame();

  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().boundariesRepainted, 1);
  CHECK_NE(after.revision, before.revision);
}

TEST(reactivity_a_value_pushed_from_a_hover_callback_is_built_in_the_same_frame) {
  Harness h({100, 40});
  Observable<bool> hovered{false};
  Reads reads;

  h.attach([&] {
    return Pointer::make({
        .behavior = HitTestBehavior::Opaque,
        .onEnter = [&hovered] { hovered.set(true); },
        .onExit = [&hovered] { hovered.set(false); },
        .child = SizedBox::make({
            .size = {100, 40},
            .child = Watch<bool>::make({
                .value = &hovered,
                .builder =
                    [&reads](BuildContext&, const bool& on) {
                      return Report::make({.into = &reads, .value = on ? 1 : 0});
                    },
            }),
        }),
    });
  });
  h.frame();

  h.binding().dispatchPointer({PointerPhase::Move, 0, PointerDeviceKind::Mouse, {50, 20}});
  h.frame();
  CHECK_EQ(reads.value, 1);
  CHECK_EQ(reads.builds, 2);

  h.binding().dispatchPointer({PointerPhase::Move, 0, PointerDeviceKind::Mouse, {500, 20}});
  h.frame();
  CHECK_EQ(reads.value, 0);
  CHECK_EQ(reads.builds, 3);
}

TEST(reactivity_steady_state_pushes_allocate_nothing) {
  Harness h;
  Observable<int> shield{100};
  Reads reads, themeReads;

  h.attach([&] {
    return Themed::make({
        .initial = Theme{},
        .child = Column::make({.children = {
                                   ThemeReader::make({.into = &themeReads}),
                                   watched(shield, reads),
                               }}),
    });
  });

  // Warm the arena's chunks, both dirty lists, the reconciliation scratch and
  // every element's dependency slot.
  for (int i = 0; i < 4; ++i) {
    shield.set(i);
    themedState(h.rootElement()).set(Theme{Color::argb(0xFF102030), 10.0f + static_cast<float>(i)});
    h.frame();
  }

  const std::size_t beforeIdle = allocationCount();
  shield.set(3);
  h.frame();
  CHECK_EQ(allocationCount() - beforeIdle, std::size_t{0});

  const std::size_t beforePush = allocationCount();
  shield.set(42);
  h.frame();
  CHECK_EQ(allocationCount() - beforePush, std::size_t{0});
  CHECK_EQ(reads.value, 42);

  const std::size_t beforeTheme = allocationCount();
  themedState(h.rootElement()).set(Theme{Color::argb(0xFF405060), 12.0f});
  h.frame();
  CHECK_EQ(allocationCount() - beforeTheme, std::size_t{0});
  CHECK_EQ(themeReads.theme.scale, 12.0f);
}

// ---------------------------------------------------------------------------
// Ambient propagation
// ---------------------------------------------------------------------------

TEST(reactivity_an_ambient_value_is_read_at_any_depth_and_the_nearest_one_wins) {
  Harness h;
  Reads outer, inner;

  h.attach([&] {
    return Ambient<Theme>::make({
        .value = Theme{Color::argb(0xFF102030), 10.0f},
        .child = Column::make({.children = {
                                   Padding::make({
                                       .padding = EdgeInsets::all(1),
                                       .child = Align::make({
                                           .child = ThemeReader::make({.into = &outer}),
                                       }),
                                   }),
                                   Ambient<Theme>::make({
                                       .value = Theme{Color::argb(0xFF405060), 20.0f},
                                       .child = ThemeReader::make({.into = &inner}),
                                   }),
                               }}),
    });
  });
  h.frame();

  CHECK_EQ(outer.theme.scale, 10.0f);
  CHECK_EQ(inner.theme.scale, 20.0f);
}

TEST(reactivity_an_ambient_change_rebuilds_its_readers_and_nothing_else) {
  Harness h;
  Reads reader, bystander;

  h.attach([&] {
    return Themed::make({
        .initial = Theme{Color::argb(0xFF102030), 10.0f},
        .child = Column::make({.children = {
                                   Padding::make({
                                       .padding = EdgeInsets::all(1),
                                       .child = ThemeReader::make({.into = &reader}),
                                   }),
                                   ThemeReader::make({.into = &bystander, .reads = false}),
                               }}),
    });
  });
  h.frame();
  CHECK_EQ(reader.builds, 1);
  CHECK_EQ(bystander.builds, 1);
  h.buildOwner().resetBuildCount();

  themedState(h.rootElement()).set(Theme{Color::argb(0xFF102030), 30.0f});
  h.frame();

  // Two elements built: the one that changed the theme, and the one that reads
  // it. The Column and the Padding between them were never visited -- the reader
  // was reached by its subscription, not by the build cascade.
  CHECK_EQ(h.buildOwner().buildCount(), 2);
  CHECK_EQ(reader.builds, 2);
  CHECK_EQ(reader.theme.scale, 30.0f);
  CHECK_EQ(bystander.builds, 1);
}

TEST(reactivity_an_equal_ambient_value_notifies_nobody) {
  Harness h;
  Reads reader;
  const Theme theme{Color::argb(0xFF102030), 10.0f};

  h.attach([&] {
    return Themed::make({.initial = theme, .child = ThemeReader::make({.into = &reader})});
  });
  h.frame();
  const std::uint64_t revision = h.frame().revision;

  themedState(h.rootElement()).set(theme);
  const Scene after = h.frame();

  CHECK_EQ(reader.builds, 1);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().paints, 0);
  CHECK_EQ(after.revision, revision);
}

TEST(reactivity_an_element_that_stops_reading_an_ambient_stops_being_rebuilt) {
  Harness h;
  Reads reader;
  bool reads = true;

  ScriptedRoot root(h, [&] {
    return Themed::make({
        .initial = Theme{Color::argb(0xFF102030), 10.0f},
        .child = ThemeReader::make({.into = &reader, .reads = reads}),
    });
  });
  h.frame();
  CHECK_EQ(themeElement(h.rootElement()).listenerCount(), std::size_t{1});

  reads = false;
  root.rebuild();
  h.frame();
  CHECK_EQ(themeElement(h.rootElement()).listenerCount(), std::size_t{0});

  const int builds = reader.builds;
  themedState(h.rootElement()).set(Theme{Color::argb(0xFF405060), 30.0f});
  h.frame();
  CHECK_EQ(reader.builds, builds);
}

TEST(reactivity_a_reader_that_leaves_the_tree_holds_no_dependency) {
  Harness h;
  Reads reader;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return Ambient<Theme>::make({
        .value = Theme{},
        .child = Column::make({.children = {
                                   visible ? ThemeReader::make({.into = &reader}) : WidgetRef{},
                               }}),
    });
  });
  h.frame();
  CHECK_EQ(themeElement(h.rootElement()).listenerCount(), std::size_t{1});

  visible = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(themeElement(h.rootElement()).listenerCount(), std::size_t{0});
}

TEST(reactivity_reading_an_ambient_that_is_not_there_is_trapped) {
  Harness h;
  Reads reader;
  CHECK_THROWS(h.attach([&] { return ThemeReader::make({.into = &reader}); }));
}

TEST(reactivity_an_ambient_scope_is_a_snapshot_not_a_chain) {
  struct Node final : InheritedElementBase {};
  Node theme, scale, innerTheme;

  InheritedScope outer, middle, inner;
  outer.extend(nullptr, widgetTypeOf<ThemeReader>(), theme);
  middle.extend(&outer, widgetTypeOf<Report>(), scale);
  inner.extend(&middle, widgetTypeOf<ThemeReader>(), innerTheme);

  // The innermost scope holds no link to the ones it extends, so reaching the
  // outermost entry through it can only be a lookup and never a walk.
  CHECK_EQ(inner.find(widgetTypeOf<Report>()), &scale);
  CHECK_EQ(inner.find(widgetTypeOf<ThemeReader>()), &innerTheme);
  CHECK_EQ(inner.size(), std::size_t{2});
  CHECK_EQ(middle.find(widgetTypeOf<ThemeReader>()), &theme);
  CHECK(inner.find(widgetTypeOf<Themed>()) == nullptr);
}
