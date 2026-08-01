#include "testing.hpp"

#include <memory>
#include <string>

#include "fltr/harness.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/focus.hpp"
#include "fltr/widgets/overlay.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

constexpr Size kSurface{200, 100};
constexpr Color kBaseColor = Color::argb(0xFF102030);
constexpr Color kEntryColor = Color::argb(0xFFA0B0C0);
constexpr Color kSecondColor = Color::argb(0xFF00FF00);

PointerEvent mouse(PointerPhase phase, Offset position) {
  return {phase, 0, PointerDeviceKind::Mouse, position};
}

void tap(Harness& h, Offset at) {
  h.binding().dispatchPointer(mouse(PointerPhase::Down, at));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, at));
}

WidgetRef painted(Color color, Size size) {
  return SizedBox::make({
      .size = size,
      .child = DecoratedBox::make({.decoration = {.color = color}}),
  });
}

/// Reads the ambient overlay at its own place in the tree, which is the only
/// place an ambient value may be read from.
class Probe final : public Configure<Probe, StatelessWidget> {
public:
  struct Args {
    Key key;
    OverlayState** found = nullptr;
    int* reads = nullptr;
    WidgetRef child;
  };

  explicit Probe(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Probe"; }

  WidgetRef build(BuildContext& context) const {
    *args_.found = Overlay::of(context);
    if (args_.reads) ++*args_.reads;
    return args_.child;
  }

private:
  Args args_;
};

/// A stateful leaf inside an entry, so that an entry's subtree can be observed
/// arriving and going away.
struct Life {
  int inits = 0;
  int disposes = 0;
  int builds = 0;
};

class Marker;

class MarkerState final : public State<Marker> {
public:
  void initState() override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;
};

class Marker final : public Configure<Marker, StatefulWidget> {
public:
  struct Args {
    Key key;
    Life* life = nullptr;
    Color color = kEntryColor;
    Size size{20, 20};
  };

  explicit Marker(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Marker"; }
  Life& life() const noexcept { return *args_.life; }
  Color color() const noexcept { return args_.color; }
  Size size() const noexcept { return args_.size; }

  std::unique_ptr<State<Marker>> createState() const { return std::make_unique<MarkerState>(); }

private:
  Args args_;
};

void MarkerState::initState() { ++widget().life().inits; }
void MarkerState::dispose() { ++widget().life().disposes; }

WidgetRef MarkerState::build(BuildContext&) {
  ++widget().life().builds;
  return painted(widget().color(), widget().size());
}

std::size_t indexOf(const std::string& haystack, Color color) {
  return haystack.find(dbg::str(color));
}

}  // namespace

// ---------------------------------------------------------------------------
// The layer itself
// ---------------------------------------------------------------------------

TEST(overlay_an_entry_paints_above_the_base_and_is_hit_tested_before_it) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  int baseTaps = 0;
  int entryTaps = 0;

  OverlayEntry entry([&entryTaps](BuildContext&) {
    return Pointer::make({
        .behavior = HitTestBehavior::Opaque,
        .onTap = [&entryTaps] { ++entryTaps; },
        .child = painted(kEntryColor, {40, 40}),
    });
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({
            .found = &overlay,
            .child = Pointer::make({
                .behavior = HitTestBehavior::Opaque,
                .onTap = [&baseTaps] { ++baseTaps; },
                .child = painted(kBaseColor, kSurface),
            }),
        }),
    });
  });
  h.frame();
  CHECK(overlay != nullptr);

  tap(h, {10, 10});
  CHECK_EQ(baseTaps, 1);

  overlay->insert(entry);
  const Scene scene = h.frame();

  const std::string commands = dumpScene(scene);
  CHECK(indexOf(commands, kBaseColor) < indexOf(commands, kEntryColor));

  // Over the entry the entry answers; beside it the base still does.
  tap(h, {10, 10});
  CHECK_EQ(entryTaps, 1);
  CHECK_EQ(baseTaps, 1);
  tap(h, {150, 80});
  CHECK_EQ(baseTaps, 2);
}

TEST(overlay_an_entry_never_changes_the_layout_below_it) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  Life life;

  OverlayEntry entry([&life](BuildContext&) {
    // Bigger than the base, so an overlay that measured its entries would be
    // dragged out of shape by this one.
    return Marker::make({.life = &life, .size = {180, 90}});
  });

  // Loose constraints, so the overlay's size is genuinely its base's and not
  // its parent's.
  ScriptedRoot root(h, [&] {
    return Align::make({
        .alignment = Alignment::topLeft(),
        .widthFactor = 1.0f,
        .heightFactor = 1.0f,
        .child = Overlay::make({
            .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, {60, 40})}),
        }),
    });
  });
  h.frame();

  Element& stack = elementFor(h.rootElement(), widgetTypeOf<OverlayStack>());
  CHECK_EQ(stack.renderObject()->size(), Size(60, 40));

  overlay->insert(entry);
  h.frame();

  CHECK_EQ(stack.renderObject()->size(), Size(60, 40));
  CHECK_EQ(life.inits, 1);
}

TEST(overlay_entries_stack_in_the_order_they_were_inserted) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  OverlayEntry lower([](BuildContext&) { return painted(kEntryColor, {40, 40}); });
  OverlayEntry upper([](BuildContext&) { return painted(kSecondColor, {40, 40}); });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, kSurface)}),
    });
  });
  h.frame();

  overlay->insert(lower);
  overlay->insert(upper);
  const std::string commands = dumpScene(h.frame());
  CHECK(indexOf(commands, kEntryColor) < indexOf(commands, kSecondColor));

  // Taken out and put back is on top, because insertion is what decides order.
  lower.remove();
  overlay->insert(lower);
  const std::string reordered = dumpScene(h.frame());
  CHECK(indexOf(reordered, kSecondColor) < indexOf(reordered, kEntryColor));
}

TEST(overlay_no_overlay_above_reports_none) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  int reads = 0;
  ScriptedRoot root(h, [&] {
    return Probe::make({
        .found = &overlay,
        .reads = &reads,
        .child = painted(kBaseColor, kSurface),
    });
  });
  h.frame();

  CHECK_EQ(reads, 1);
  CHECK(overlay == nullptr);
  CHECK(dumpElementTree(h.rootElement()).find("OverlayScope") == std::string::npos);
}

// ---------------------------------------------------------------------------
// What inserting costs
// ---------------------------------------------------------------------------

TEST(overlay_inserting_an_entry_does_not_rebuild_the_tree_below) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  Life base;
  Life shown;

  OverlayEntry entry([&shown](BuildContext&) { return Marker::make({.life = &shown}); });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = Marker::make({.life = &base})}),
    });
  });
  h.frame();
  CHECK_EQ(base.builds, 1);

  overlay->insert(entry);
  h.frame();

  // The overlay and the entry built; the tree being overlaid was not touched,
  // because the ref naming it went stale and reconciliation skipped it.
  CHECK_EQ(base.builds, 1);
  CHECK_EQ(shown.builds, 1);

  entry.markNeedsBuild();
  h.frame();
  CHECK_EQ(shown.builds, 2);
  CHECK_EQ(base.builds, 1);
  // One element rebuilt: the entry's own host, and nothing above or below it.
  CHECK_EQ(h.buildOwner().buildCount(), 1);
}

TEST(overlay_a_steady_frame_with_an_overlay_does_nothing) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  OverlayEntry entry([](BuildContext&) { return painted(kEntryColor, {20, 20}); });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, kSurface)}),
    });
  });
  h.frame();
  overlay->insert(entry);
  const Scene shown = h.frame();

  const std::size_t before = allocationCount();
  const Scene again = h.frame(0.016f);
  CHECK_EQ(again.revision, shown.revision);
  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(allocationCount() - before, std::size_t{0});
  CHECK(!h.binding().needsFrame());
}

// ---------------------------------------------------------------------------
// Lifetimes
// ---------------------------------------------------------------------------

TEST(overlay_removing_an_entry_discards_its_subtree) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  Life life;

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, kSurface)}),
    });
  });
  h.frame();

  {
    OverlayEntry entry([&life](BuildContext&) { return Marker::make({.life = &life}); });
    overlay->insert(entry);
    h.frame();
    CHECK_EQ(life.inits, 1);
    CHECK_EQ(overlay->entryCount(), std::size_t{1});

    entry.remove();
    h.frame();
    CHECK_EQ(life.disposes, 1);
    CHECK(!entry.inserted());
  }
  CHECK_EQ(overlay->entryCount(), std::size_t{0});
}

TEST(overlay_an_entry_destroyed_while_inserted_removes_itself) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  Life life;

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, kSurface)}),
    });
  });
  h.frame();

  {
    OverlayEntry entry([&life](BuildContext&) { return Marker::make({.life = &life}); });
    overlay->insert(entry);
    h.frame();
    CHECK_EQ(overlay->entryCount(), std::size_t{1});
  }

  CHECK_EQ(overlay->entryCount(), std::size_t{0});
  h.frame();
  CHECK_EQ(life.disposes, 1);
}

TEST(overlay_an_entry_outliving_its_overlay_is_let_go_of) {
  Life life;
  OverlayEntry entry([&life](BuildContext&) { return Marker::make({.life = &life}); });

  {
    Harness h(kSurface);
    OverlayState* overlay = nullptr;
    ScriptedRoot root(h, [&] {
      return Overlay::make({
          .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, kSurface)}),
      });
    });
    h.frame();
    overlay->insert(entry);
    h.frame();
    CHECK(entry.inserted());
  }

  // The overlay let go of it rather than leaving it naming a State that is gone.
  CHECK(!entry.inserted());
  CHECK_EQ(life.disposes, 1);
}

namespace {

/// Owns an entry the way a consumer would: inserted from its first build, and
/// destroyed with the State that holds it.
class Owner;

class OwnerState final : public State<Owner> {
public:
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

private:
  std::unique_ptr<OverlayEntry> entry_;
};

class Owner final : public Configure<Owner, StatefulWidget> {
public:
  struct Args {
    Key key;
    Life* life = nullptr;
    Life* entryLife = nullptr;
  };

  explicit Owner(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Owner"; }
  Life& life() const noexcept { return *args_.life; }
  Life* entryLife() const noexcept { return args_.entryLife; }

  std::unique_ptr<State<Owner>> createState() const { return std::make_unique<OwnerState>(); }

private:
  Args args_;
};

void OwnerState::dispose() { ++widget().life().disposes; }

WidgetRef OwnerState::build(BuildContext& context) {
  OverlayState* overlay = Overlay::of(context);
  if (!entry_ && overlay) {
    Life* life = widget().entryLife();
    entry_ = std::make_unique<OverlayEntry>(
        [life](BuildContext&) { return Marker::make({.life = life}); });
    overlay->insert(*entry_);
  }
  return painted(kBaseColor, {20, 20});
}

}  // namespace

TEST(overlay_an_entry_destroyed_mid_reconciliation_is_not_built) {
  Harness h(kSurface);
  Life owner;
  Life shown;
  bool present = true;

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = present ? Owner::make({.life = &owner, .entryLife = &shown})
                         : painted(kBaseColor, kSurface),
    });
  });
  h.frame();
  CHECK_EQ(shown.inits, 1);

  // The base is reconciled before the entries, so the entry dies with its owner
  // in the middle of the same build that would otherwise go on to rebuild it.
  present = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(owner.disposes, 1);
  CHECK_EQ(shown.disposes, 1);
  CHECK(dumpElementTree(h.rootElement()).find("OverlayEntryHost") == std::string::npos);
}

TEST(overlay_an_entry_put_back_builds_a_fresh_subtree) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  Life life;
  OverlayEntry entry([&life](BuildContext&) { return Marker::make({.life = &life}); });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = painted(kBaseColor, kSurface)}),
    });
  });
  h.frame();

  overlay->insert(entry);
  h.frame();
  entry.remove();
  h.frame();
  overlay->insert(entry);
  h.frame();

  // Two insertions, two subtrees: an entry taken out and put back does not
  // resume the State it had.
  CHECK_EQ(life.inits, 2);
  CHECK_EQ(life.disposes, 1);
}

TEST(overlay_an_entry_can_keep_the_keyboard_to_itself) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  FocusScopeNode screen;
  FocusScopeNode menu;
  FocusNode behind;
  FocusNode first;
  FocusNode second;

  const auto item = [](FocusNode* node, bool autofocus = false) {
    return Focus::make({
        .node = node,
        .autofocus = autofocus,
        .child = SizedBox::make({.size = {40, 20}}),
    });
  };

  OverlayEntry entry([&](BuildContext&) {
    // A scope of its own is all a menu needs to trap traversal, which is M11's
    // property reached from an overlay rather than from the tree below it.
    return FocusScope::make({
        .node = &menu,
        .child = Column::make({
            .mainAxisSize = MainAxisSize::Min,
            .children = {item(&first, true), item(&second)},
        }),
    });
  });

  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &screen,
        .child = Overlay::make({
            .child = Probe::make({.found = &overlay, .child = item(&behind, true)}),
        }),
    });
  });
  h.frame();
  CHECK(behind.hasPrimaryFocus());

  overlay->insert(entry);
  h.frame();
  CHECK(first.hasPrimaryFocus());

  h.binding().dispatchKey({.type = KeyEventType::Down, .logical = LogicalKey::Tab});
  CHECK(second.hasPrimaryFocus());
  h.binding().dispatchKey({.type = KeyEventType::Down, .logical = LogicalKey::Tab});
  CHECK(first.hasPrimaryFocus());

  // Closing it leaves the focus on the screen's own scope -- M11's answer for a
  // focused subtree going away -- so the next key moves within the screen again
  // rather than into something that is gone.
  entry.remove();
  h.frame();
  CHECK(!menu.attached());
  CHECK(!first.hasFocus());
  CHECK(screen.hasFocus());

  h.binding().dispatchKey({.type = KeyEventType::Down, .logical = LogicalKey::Tab});
  CHECK(behind.hasPrimaryFocus());
}

// ---------------------------------------------------------------------------
// Anchoring
// ---------------------------------------------------------------------------

namespace {

/// The anchor sits at `left`, `width` wide and 20 tall, inside the base.
WidgetRef anchoredBase(AnchorLink* link, float left, float width = 30.0f) {
  return Stack::make({
      .fit = StackFit::Expand,
      .children = {Positioned::make({
          .left = left,
          .top = 20.0f,
          .width = width,
          .height = 20.0f,
          .child = Anchor::make({.link = link, .child = painted(kBaseColor, {width, 20})}),
      })},
  });
}

Offset entryOffset(Harness& h) {
  Element& anchored = elementFor(h.rootElement(), widgetTypeOf<Anchored>());
  return static_cast<RenderShiftedBox*>(anchored.renderObject())->childOffset();
}

}  // namespace

TEST(overlay_an_anchored_entry_hangs_from_the_rectangle_it_names) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  AnchorLink link;
  float left = 40.0f;

  OverlayEntry entry([&link](BuildContext&) {
    return Anchored::make({.link = &link, .child = painted(kEntryColor, {50, 25})});
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = anchoredBase(&link, left)}),
    });
  });
  h.frame();
  CHECK_EQ(link.rect(), Rect::fromLTWH(40, 20, 30, 20));

  overlay->insert(entry);
  h.frame();
  // Below the anchor, left edges aligned: a dropdown.
  CHECK_EQ(entryOffset(h), Offset(40, 40));

  // The anchor moves, and the entry is placed against where it now is -- in the
  // same frame, because the anchor says so while it lays out.
  left = 100.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(link.rect(), Rect::fromLTWH(100, 20, 30, 20));
  CHECK_EQ(entryOffset(h), Offset(100, 40));
}

TEST(overlay_an_anchored_entry_is_placed_by_the_sides_it_was_given) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  AnchorLink link;

  OverlayEntry entry([&link](BuildContext&) {
    return Anchored::make({
        .link = &link,
        .anchorSide = Alignment::topCenter(),
        .childSide = Alignment::bottomCenter(),
        .offset = {0, -5},
        .child = painted(kEntryColor, {20, 10}),
    });
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = anchoredBase(&link, 40)}),
    });
  });
  h.frame();
  overlay->insert(entry);
  h.frame();

  // A tooltip: centred over the anchor's top edge, five pixels clear of it.
  CHECK_EQ(entryOffset(h), Offset(45, 5));
}

TEST(overlay_an_anchored_entry_is_kept_on_screen) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  AnchorLink link;
  bool keepOnScreen = true;

  OverlayEntry entry([&](BuildContext&) {
    return Anchored::make({
        .link = &link,
        .keepOnScreen = keepOnScreen,
        .child = painted(kEntryColor, {60, 40}),
    });
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = anchoredBase(&link, 170)}),
    });
  });
  h.frame();
  overlay->insert(entry);
  h.frame();

  // Hung from x=170 it would run 30 pixels off a 200 wide surface.
  CHECK_EQ(entryOffset(h), Offset(140, 40));

  keepOnScreen = false;
  entry.markNeedsBuild();
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(170, 40));
}

TEST(overlay_an_anchor_that_repaints_on_its_own_still_reports_where_it_is) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  AnchorLink link;
  float width = 30.0f;

  OverlayEntry entry([&link](BuildContext&) {
    return Anchored::make({
        .link = &link,
        .anchorSide = Alignment::bottomRight(),
        .child = painted(kEntryColor, {40, 20}),
    });
  });

  // Its own boundary, so the root's display list is not re-recorded when
  // anything inside it changes: what re-places the entry is the anchor saying
  // it laid out, and nothing else.
  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({
            .found = &overlay,
            .child = RepaintBoundary::make({.child = anchoredBase(&link, 40, width)}),
        }),
    });
  });
  h.frame();
  overlay->insert(entry);
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(70, 40));

  width = 80.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(120, 40));
}

TEST(overlay_an_anchor_moved_without_laying_out_is_followed_when_it_says_so) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  AnchorLink link;
  float left = 40.0f;

  OverlayEntry entry([&link](BuildContext&) {
    return Anchored::make({.link = &link, .child = painted(kEntryColor, {40, 20})});
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({
            .found = &overlay,
            .child = RepaintBoundary::make({.child = anchoredBase(&link, left)}),
        }),
    });
  });
  h.frame();
  overlay->insert(entry);
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(40, 40));

  // Moved by its parent without laying out itself, inside a boundary of its own
  // -- which is what scrolling looks like. The link knows where it is; nothing
  // has told the entry to look again.
  left = 100.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(link.rect(), Rect::fromLTWH(100, 20, 30, 20));
  CHECK_EQ(entryOffset(h), Offset(40, 40));

  // The stated remedy, and the whole of it.
  link.markMoved();
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(100, 40));
}

TEST(overlay_an_anchored_entry_whose_link_dies_stays_put_rather_than_reading_it) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  auto link = std::make_unique<AnchorLink>();
  bool withAnchor = true;

  OverlayEntry entry([&link](BuildContext&) {
    return Anchored::make({.link = link.get(), .child = painted(kEntryColor, {20, 10})});
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({
            .found = &overlay,
            .child = withAnchor ? anchoredBase(link.get(), 40) : painted(kBaseColor, kSurface),
        }),
    });
  });
  h.frame();
  overlay->insert(entry);
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(40, 40));

  // The anchor goes away, and with it the rectangle: what is left has none.
  withAnchor = false;
  root.rebuild();
  h.frame();
  CHECK(!link->attached());
  CHECK_EQ(entryOffset(h), Offset(0, 0));

  // And the link itself can die under a live entry.
  link.reset();
  entry.markNeedsBuild();
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(0, 0));
}

TEST(overlay_a_link_destroyed_under_a_live_anchor_is_let_go_of) {
  Harness h(kSurface);
  OverlayState* overlay = nullptr;
  auto owned = std::make_unique<AnchorLink>();
  AnchorLink* link = owned.get();

  OverlayEntry entry([&link](BuildContext&) {
    return Anchored::make({.link = link, .child = painted(kEntryColor, {40, 20})});
  });

  ScriptedRoot root(h, [&] {
    return Overlay::make({
        .child = Probe::make({.found = &overlay, .child = anchoredBase(link, 40)}),
    });
  });
  h.frame();
  overlay->insert(entry);
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(40, 40));

  // Gone while both the anchor naming it and the entry following it are still
  // mounted, and then a frame that lays both of them out. Neither reads it.
  owned.reset();
  h.binding().setSurface({210, 100});
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(0, 0));

  // A build that still named the dead link would be the rule broken outright,
  // so the tree stops naming it.
  link = nullptr;
  root.rebuild();
  h.frame();
  CHECK_EQ(entryOffset(h), Offset(0, 0));
}
