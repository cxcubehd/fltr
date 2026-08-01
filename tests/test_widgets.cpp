#include "testing.hpp"

#include <string>
#include <vector>

#include "fltr/harness.hpp"
#include "fltr/widgets/binding.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

/// Everything a test needs to know about one State object's lifetime. Each
/// keyed child in a test owns one, so survival across a rebuild is observable:
/// an element that survived was never re-initialised, and `serial` still names
/// the same State instance.
struct Life {
  int inits = 0;
  int updates = 0;
  int disposes = 0;
  int builds = 0;
  /// Which State instance last built this configuration. Unchanged across a
  /// rebuild means the element survived; a different value means the
  /// configuration was handed to some other element's State.
  int builtBy = 0;
  int lastValue = 0;
};

int gNextSerial = 0;

class Tracked;

class TrackedState final : public State<Tracked> {
public:
  void initState() override;
  void didUpdateWidget(const Tracked& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

  int serial() const noexcept { return serial_; }
  void bump() {
    setState([this] { ++bumps_; });
  }

private:
  int bumps_ = 0;
  int serial_ = 0;
};

/// A stateful leaf whose State is observable and whose configuration reaches
/// layout, so both reconciliation and its render-tree consequences are testable.
class Tracked final : public Configure<Tracked, StatefulWidget> {
public:
  struct Args {
    Key key;
    Life* life = nullptr;
    float width = 10.0f;
    Color color = Color::argb(0xFF204060);
  };

  explicit Tracked(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Tracked"; }
  Life& life() const noexcept { return *args_.life; }
  float width() const noexcept { return args_.width; }
  Color color() const noexcept { return args_.color; }

  std::unique_ptr<State<Tracked>> createState() const {
    return std::make_unique<TrackedState>();
  }

private:
  Args args_;
};

void TrackedState::initState() {
  serial_ = ++gNextSerial;
  ++widget().life().inits;
}

void TrackedState::didUpdateWidget(const Tracked& previous) {
  ++widget().life().updates;
  (void)previous;
}

void TrackedState::dispose() { ++widget().life().disposes; }

WidgetRef TrackedState::build(BuildContext&) {
  Life& life = widget().life();
  ++life.builds;
  life.builtBy = serial_;
  life.lastValue = static_cast<int>(widget().width());
  return SizedBox::make({
      .size = {widget().width() + static_cast<float>(bumps_), 8.0f},
      .child = DecoratedBox::make({.decoration = {.color = widget().color()}}),
  });
}

class Restless;

/// Deliberately non-converging: dirties itself from inside its own build.
class RestlessState final : public State<Restless> {
public:
  WidgetRef build(BuildContext&) override {
    setState([] {});
    return SizedBox::make({.size = {1, 1}});
  }
};

class Restless final : public Configure<Restless, StatefulWidget> {
public:
  struct Args {
    Key key;
  };

  explicit Restless(const Args& args) : Configure(args.key) {}

  const char* name() const noexcept override { return "Restless"; }
  std::unique_ptr<State<Restless>> createState() const {
    return std::make_unique<RestlessState>();
  }
};

/// A stateless wrapper, so tests exercise a build step that is not a render
/// object and a chain the render tree has to see through.
class Frame final : public Configure<Frame, StatelessWidget> {
public:
  struct Args {
    Key key;
    EdgeInsets padding;
    int* buildCount = nullptr;
    WidgetRef child;
  };

  explicit Frame(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Frame"; }

  WidgetRef build(BuildContext&) const {
    if (args_.buildCount) ++*args_.buildCount;
    return Padding::make({.padding = args_.padding, .child = args_.child});
  }

private:
  Args args_;
};

TrackedState& trackedState(Element& root, Key key = Key::none()) {
  auto& element =
      static_cast<StatefulElement<Tracked>&>(elementFor(root, widgetTypeOf<Tracked>(), key));
  return static_cast<TrackedState&>(element.state());
}

const RenderFlex& flexIn(const RenderObject& root) {
  const RenderFlex* found = nullptr;
  const auto walk = [&found](auto&& self, const RenderObject& node) -> void {
    if (found) return;
    if (const auto* flex = dynamic_cast<const RenderFlex*>(&node)) {
      found = flex;
      return;
    }
    node.visitChildren([&](RenderObject& child) { self(self, child); });
  };
  walk(walk, root);
  FLTR_EXPECTS(found != nullptr, "no flex in this tree");
  return *found;
}

std::vector<Color> childColors(const RenderFlex& flex) {
  std::vector<Color> colors;
  for (std::size_t i = 0; i < flex.childCount(); ++i) {
    const RenderObject* node = &flex.childAt(i);
    const RenderDecoratedBox* decorated = nullptr;
    const auto walk = [&decorated](auto&& self, const RenderObject& n) -> void {
      if (decorated) return;
      if (const auto* d = dynamic_cast<const RenderDecoratedBox*>(&n)) {
        decorated = d;
        return;
      }
      n.visitChildren([&](RenderObject& child) { self(self, child); });
    };
    walk(walk, *node);
    colors.push_back(decorated ? decorated->decoration().color : Color::transparent());
  }
  return colors;
}

}  // namespace

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

TEST(widgets_first_frame_builds_lays_out_and_paints) {
  Harness h;
  h.attach([] {
    return Padding::make({
        .padding = EdgeInsets::all(8),
        .child = Align::make({
            .alignment = Alignment::topLeft(),
            .child = SizedBox::make({.size = {40, 20}}),
        }),
    });
  });

  const Scene scene = h.frame();

  CHECK_EQ(dumpElementTree(h.rootElement()),
           "View\n"
           "  Padding\n"
           "    Align\n"
           "      SizedBox\n");
  CHECK_EQ(h.view().size(), (Size{200, 100}));
  CHECK_EQ(scene.surface, (Size{200, 100}));
  CHECK(scene.root != nullptr);
}

TEST(widgets_a_frame_with_no_state_change_does_no_work) {
  Harness h;
  h.attach([] { return SizedBox::make({.size = {40, 20}}); });
  const Scene first = h.frame();

  h.buildOwner().resetBuildCount();
  const Scene second = h.frame();

  CHECK(!h.binding().needsFrame());
  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().paints, 0);
  CHECK_EQ(second.revision, first.revision);
}

TEST(widgets_steady_state_frames_allocate_nothing) {
  Harness h;
  Life life;
  ScriptedRoot root(h, [&] {
    return Column::make({.children = {Tracked::make({.life = &life, .width = 30})}});
  });

  // Warm every buffer: the arena's chunks, both build scratch lists, the
  // reconciliation scratch, and the render tree's dirty lists.
  for (int i = 0; i < 4; ++i) {
    root.rebuild();
    h.frame();
  }

  const std::size_t beforeIdle = fltrtest::allocationCount();
  h.frame();
  CHECK_EQ(fltrtest::allocationCount() - beforeIdle, std::size_t{0});

  const int buildsBefore = life.builds;
  const std::size_t beforeRebuild = fltrtest::allocationCount();
  root.rebuild();
  h.frame();
  CHECK_EQ(fltrtest::allocationCount() - beforeRebuild, std::size_t{0});
  CHECK_EQ(life.builds, buildsBefore + 1);
}

// ---------------------------------------------------------------------------
// Rebuild without mutation
// ---------------------------------------------------------------------------

TEST(widgets_rebuild_with_equivalent_configuration_mutates_no_render_object) {
  Harness h;
  Life life;
  ScriptedRoot root(h, [&] {
    return Padding::make({
        .padding = EdgeInsets::all(8),
        .child = Tracked::make({.life = &life, .width = 30}),
    });
  });
  h.frame();

  const std::uint64_t revision = h.frame().revision;
  root.rebuild();
  const Scene after = h.frame();

  // The element tree rebuilt; the render tree did not move.
  CHECK_EQ(life.builds, 2);
  CHECK_EQ(life.updates, 1);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().paints, 0);
  CHECK_EQ(after.revision, revision);
}

TEST(widgets_rebuilding_a_container_with_parent_data_mutates_no_render_object) {
  Harness h({300, 50});
  ScriptedRoot root(h, [&] {
    return Row::make({.children = {
                          SizedBox::make({.size = {60, 10}}),
                          Flexible::make({.flex = 1, .child = SizedBox::make({.size = {0, 10}})}),
                      }});
  });
  h.frame();
  const std::uint64_t revision = h.frame().revision;

  root.rebuild();
  const Scene after = h.frame();

  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().paints, 0);
  CHECK_EQ(after.revision, revision);
  CHECK_EQ(flexIn(h.view()).dataAt(1).flex, 1);
}

TEST(widgets_a_parent_data_widget_in_the_wrong_container_is_trapped) {
  Harness h({200, 100});
  CHECK_THROWS(h.attach([] {
    return Stack::make({.children = {
                            Flexible::make({.flex = 1, .child = SizedBox::make({.size = {5, 5}})}),
                        }});
  }));
}

TEST(widgets_changed_configuration_reaches_layout) {
  Harness h;
  Life life;
  float width = 30;
  ScriptedRoot root(h, [&] {
    return Align::make({
        .alignment = Alignment::topLeft(),
        .child = Tracked::make({.life = &life, .width = width}),
    });
  });
  h.frame();
  CHECK_EQ(h.view().child()->size(), (Size{200, 100}));

  width = 55;
  root.rebuild();
  h.frame();

  const RenderBox& aligned = *static_cast<RenderShiftedBox&>(*h.view().child()).child();
  CHECK_EQ(aligned.size(), (Size{55, 8}));
}

TEST(widgets_setState_rebuilds_only_its_own_element) {
  Harness h;
  Life outer;
  Life inner;
  int frameBuilds = 0;

  ScriptedRoot root(h, [&] {
    return Frame::make({
        .padding = EdgeInsets::all(4),
        .buildCount = &frameBuilds,
        .child = Column::make({.children = {
                                   Tracked::make({.key = Key::of("outer"), .life = &outer}),
                                   Tracked::make({.key = Key::of("inner"), .life = &inner}),
                               }}),
    });
  });
  h.frame();
  CHECK_EQ(frameBuilds, 1);

  h.buildOwner().resetBuildCount();
  trackedState(h.rootElement(), Key::of("inner")).bump();
  h.frame();

  CHECK_EQ(h.buildOwner().buildCount(), 1);
  CHECK_EQ(frameBuilds, 1);
  CHECK_EQ(outer.builds, 1);
  CHECK_EQ(inner.builds, 2);
}

// ---------------------------------------------------------------------------
// Keyed reconciliation
// ---------------------------------------------------------------------------

TEST(widgets_keyed_children_survive_reordering) {
  Harness h;
  Life a;
  Life b;
  Life c;
  bool reversed = false;

  ScriptedRoot root(h, [&] {
    const WidgetRef first = Tracked::make({.key = Key::of("a"), .life = &a, .width = 10});
    const WidgetRef second = Tracked::make({.key = Key::of("b"), .life = &b, .width = 20});
    const WidgetRef third = Tracked::make({.key = Key::of("c"), .life = &c, .width = 30});
    return Column::make({.children = reversed ? WidgetList{third, second, first}
                                              : WidgetList{first, second, third}});
  });
  h.frame();

  const int builtA = a.builtBy;
  const int builtC = c.builtBy;
  const RenderFlex& column = flexIn(h.view());
  const RenderBox* renderA = &column.childAt(0);
  const RenderBox* renderC = &column.childAt(2);

  reversed = true;
  root.rebuild();
  h.frame();

  // Same State objects, same render objects, in the new order.
  CHECK_EQ(a.inits, 1);
  CHECK_EQ(c.inits, 1);
  CHECK_EQ(a.disposes, 0);
  CHECK_EQ(c.disposes, 0);
  CHECK_EQ(a.builtBy, builtA);
  CHECK_EQ(c.builtBy, builtC);
  CHECK_EQ(&column.childAt(0), renderC);
  CHECK_EQ(&column.childAt(2), renderA);
}

TEST(widgets_unkeyed_children_reconcile_by_position_and_discard_state) {
  Harness h;
  Life a;
  Life b;
  bool swapped = false;

  ScriptedRoot root(h, [&] {
    const WidgetRef first = Tracked::make({.life = &a, .width = 10});
    const WidgetRef second = Tracked::make({.life = &b, .width = 20});
    return Column::make({.children = swapped ? WidgetList{second, first} : WidgetList{first, second}});
  });
  h.frame();

  const int builtA = a.builtBy;

  swapped = true;
  root.rebuild();
  h.frame();

  // Nothing was created or destroyed: position 0 kept its element and was
  // handed b's configuration, so the State that used to back a now backs b.
  CHECK_EQ(a.inits, 1);
  CHECK_EQ(b.inits, 1);
  CHECK_EQ(a.disposes, 0);
  CHECK_EQ(b.disposes, 0);
  CHECK_EQ(b.updates, 1);
  CHECK_EQ(b.builtBy, builtA);
  CHECK_EQ(b.lastValue, 20);
}

TEST(widgets_a_removed_keyed_child_is_disposed_and_its_render_object_dropped) {
  Harness h;
  Life a;
  Life b;
  bool keepB = true;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             Tracked::make({.key = Key::of("a"), .life = &a}),
                             keepB ? Tracked::make({.key = Key::of("b"), .life = &b}) : WidgetRef{},
                         }});
  });
  h.frame();
  CHECK_EQ(flexIn(h.view()).childCount(), std::size_t{2});

  keepB = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(b.disposes, 1);
  CHECK_EQ(a.disposes, 0);
  CHECK_EQ(flexIn(h.view()).childCount(), std::size_t{1});
}

TEST(widgets_inserting_in_the_middle_keeps_the_neighbours) {
  Harness h;
  Life a;
  Life b;
  Life c;
  bool withMiddle = false;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             Tracked::make({.key = Key::of("a"), .life = &a}),
                             withMiddle ? Tracked::make({.key = Key::of("b"), .life = &b})
                                        : WidgetRef{},
                             Tracked::make({.key = Key::of("c"), .life = &c}),
                         }});
  });
  h.frame();
  const int builtA = a.builtBy;
  const int builtC = c.builtBy;

  withMiddle = true;
  root.rebuild();
  h.frame();

  CHECK_EQ(a.builtBy, builtA);
  CHECK_EQ(c.builtBy, builtC);
  CHECK_EQ(a.inits, 1);
  CHECK_EQ(c.inits, 1);
  CHECK_EQ(b.inits, 1);
  CHECK_EQ(flexIn(h.view()).childCount(), std::size_t{3});
}

TEST(widgets_changing_type_discards_the_element_and_its_state) {
  Harness h;
  Life life;
  bool tracked = true;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {tracked ? Tracked::make({.life = &life, .width = 12})
                                              : SizedBox::make({.size = {5, 5}})}});
  });
  h.frame();
  CHECK_EQ(life.inits, 1);

  tracked = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(life.disposes, 1);
  CHECK_EQ(flexIn(h.view()).childAt(0).size(), (Size{5, 5}));
}

TEST(widgets_a_null_entry_in_a_children_list_is_dropped) {
  Harness h;
  Life a;
  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             WidgetRef{},
                             Tracked::make({.life = &a}),
                             WidgetRef{},
                         }});
  });
  h.frame();

  CHECK_EQ(flexIn(h.view()).childCount(), std::size_t{1});
  CHECK_EQ(a.inits, 1);
}

TEST(widgets_a_single_child_slot_accepts_null) {
  Harness h;
  Life life;
  bool present = true;

  ScriptedRoot root(h, [&] {
    return Padding::make({
        .padding = EdgeInsets::all(4),
        .child = present ? Tracked::make({.life = &life}) : WidgetRef{},
    });
  });
  h.frame();

  present = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(life.disposes, 1);
  const auto& padding = static_cast<RenderPadding&>(*h.view().child());
  CHECK(padding.child() == nullptr);
}

TEST(widgets_reordering_moves_render_children_rather_than_recreating_them) {
  Harness h;
  Life a;
  Life b;
  Life c;
  bool rotated = false;

  ScriptedRoot root(h, [&] {
    const WidgetRef first =
        Tracked::make({.key = Key::of(1), .life = &a, .color = Color::argb(0xFFFF0000)});
    const WidgetRef second =
        Tracked::make({.key = Key::of(2), .life = &b, .color = Color::argb(0xFF00FF00)});
    const WidgetRef third =
        Tracked::make({.key = Key::of(3), .life = &c, .color = Color::argb(0xFF0000FF)});
    return Column::make({.children = rotated ? WidgetList{second, third, first}
                                             : WidgetList{first, second, third}});
  });
  h.frame();

  CHECK_EQ(childColors(flexIn(h.view())),
           (std::vector<Color>{Color::argb(0xFFFF0000), Color::argb(0xFF00FF00),
                               Color::argb(0xFF0000FF)}));

  rotated = true;
  root.rebuild();
  h.frame();

  CHECK_EQ(childColors(flexIn(h.view())),
           (std::vector<Color>{Color::argb(0xFF00FF00), Color::argb(0xFF0000FF),
                               Color::argb(0xFFFF0000)}));
  CHECK_EQ(a.inits + b.inits + c.inits, 3);
  CHECK_EQ(a.disposes + b.disposes + c.disposes, 0);
}

// ---------------------------------------------------------------------------
// Parent data
// ---------------------------------------------------------------------------

TEST(widgets_flexible_distributes_main_axis_space) {
  Harness h({300, 50});
  h.attach([] {
    return Row::make({.children = {
                          SizedBox::make({.size = {60, 10}}),
                          Flexible::make({.flex = 1, .child = SizedBox::make({.size = {0, 10}})}),
                          Flexible::make({.flex = 2, .child = SizedBox::make({.size = {0, 10}})}),
                      }});
  });
  h.frame();

  const RenderFlex& row = flexIn(h.view());
  CHECK_EQ(row.childAt(0).size().width, 60.0f);
  CHECK_EQ(row.childAt(1).size().width, 80.0f);
  CHECK_EQ(row.childAt(2).size().width, 160.0f);
}

TEST(widgets_removing_flexible_clears_the_parent_data_it_had_set) {
  Harness h({300, 50});
  bool flexible = true;
  ScriptedRoot root(h, [&] {
    const WidgetRef box = SizedBox::make({.size = {40, 10}});
    return Row::make({.children = {
                          flexible ? Flexible::make({.flex = 1, .child = box}) : box,
                          SizedBox::make({.size = {60, 10}}),
                      }});
  });
  h.frame();
  CHECK_EQ(flexIn(h.view()).childAt(0).size().width, 240.0f);

  flexible = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(flexIn(h.view()).childAt(0).size().width, 40.0f);
  CHECK_EQ(flexIn(h.view()).dataAt(0).flex, 0);
}

TEST(widgets_flexible_applies_through_an_intervening_stateless_widget) {
  Harness h({300, 50});
  h.attach([] {
    return Row::make({.children = {
                          SizedBox::make({.size = {100, 10}}),
                          Flexible::make({
                              .flex = 1,
                              .child = Frame::make({.padding = EdgeInsets::all(5),
                                                    .child = SizedBox::make({.size = {0, 10}})}),
                          }),
                      }});
  });
  h.frame();

  CHECK_EQ(flexIn(h.view()).dataAt(1).flex, 1);
  CHECK_EQ(flexIn(h.view()).childAt(1).size().width, 200.0f);
}

TEST(widgets_positioned_places_a_child_within_a_stack) {
  Harness h({200, 100});
  h.attach([] {
    return Stack::make({.fit = StackFit::Expand,
                        .children = {
                            SizedBox::make({.size = {10, 10}}),
                            Positioned::make({
                                .left = 20.0f,
                                .top = 30.0f,
                                .width = 40.0f,
                                .height = 15.0f,
                                .child = SizedBox::make({.size = {1, 1}}),
                            }),
                        }});
  });
  h.frame();

  const auto& stack = static_cast<const RenderStack&>(*h.view().child());
  CHECK_EQ(stack.childOffsetAt(1), (Offset{20, 30}));
  CHECK_EQ(stack.childAt(1).size(), (Size{40, 15}));
}

// ---------------------------------------------------------------------------
// Layout through the widget layer
// ---------------------------------------------------------------------------

TEST(widgets_layout_matches_hand_computed_expectations) {
  Harness h({200, 100});
  h.attach([] {
    return Padding::make({
        .padding = EdgeInsets::all(10),
        .child = Row::make({
            .mainAxisAlignment = MainAxisAlignment::SpaceBetween,
            .crossAxisAlignment = CrossAxisAlignment::Start,
            .children = {SizedBox::make({.size = {40, 20}}), SizedBox::make({.size = {60, 30}})},
        }),
    });
  });
  h.frame();

  // 200x100 surface, 10 padding on every side -> a 180x80 row.
  const RenderFlex& row = flexIn(h.view());
  CHECK_EQ(row.size(), (Size{180, 80}));
  CHECK_EQ(row.childOffsetAt(0), (Offset{0, 0}));
  CHECK_EQ(row.childOffsetAt(1), (Offset{120, 0}));
}

TEST(widgets_text_measures_through_the_service_and_releases_its_paragraph) {
  MonospaceTextService text;
  {
    WidgetBinding binding({200, 100}, text);
    binding.attachRoot([] {
      return Align::make({
          .alignment = Alignment::topLeft(),
          .child = Text::make({.text = "hello", .style = {.size = 10.0f}}),
      });
    });
    binding.drawFrame();

    // Five characters at a 0.5 advance ratio of a 10pt font.
    const RenderBox& paragraph =
        *static_cast<RenderShiftedBox&>(*binding.renderView()->child()).child();
    CHECK_EQ(paragraph.size().width, 25.0f);
    CHECK_EQ(text.liveParagraphs(), std::size_t{1});
  }
  CHECK_EQ(text.liveParagraphs(), std::size_t{0});
}

TEST(widgets_the_painting_widgets_wrap_their_subtree_in_order) {
  Harness h({100, 50});
  h.attach([] {
    return Align::make({
        .alignment = Alignment::topLeft(),
        .child = Opacity::make({
            .opacity = 0.5f,
            .child = ClipRect::make({
                .radius = BorderRadius::all(4),
                .child = Transform::make({
                    .transform = Transform2D::scaling(2, 2),
                    .child = ConstrainedBox::make({
                        .constraints = BoxConstraints::tightFor(20, 10),
                        .child = DecoratedBox::make(
                            {.decoration = {.color = Color::argb(0xFF00FF00)}}),
                    }),
                }),
            }),
        }),
    });
  });
  const Scene scene = h.frame();

  CHECK_EQ(dumpDisplayList(*scene.root),
           "PushOpacity 0.5\n"
           "  PushClipRect [0,0 20x10] r=4\n"
           "    PushTransform [2 0 -10; 0 2 -5]\n"
           "      DrawRRect [0,0 20x10] #FF00FF00\n"
           "    Pop\n"
           "  Pop\n"
           "Pop\n");
}

TEST(widgets_surface_resize_relayouts_the_tree) {
  Harness h({200, 100});
  h.attach([] { return Align::make({.child = SizedBox::make({.size = {40, 20}})}); });
  h.frame();
  CHECK_EQ(h.view().size(), (Size{200, 100}));

  h.binding().setSurface({320, 240});
  h.frame();

  CHECK_EQ(h.view().size(), (Size{320, 240}));
  const auto& align = static_cast<RenderShiftedBox&>(*h.view().child());
  CHECK_EQ(align.childOffset(), (Offset{140, 110}));
}

// ---------------------------------------------------------------------------
// Build phase mechanics
// ---------------------------------------------------------------------------

TEST(widgets_build_runs_shallowest_first_so_a_parent_subsumes_its_child) {
  Harness h;
  Life outer;
  Life inner;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             Tracked::make({.key = Key::of("o"), .life = &outer}),
                             Tracked::make({.key = Key::of("i"), .life = &inner}),
                         }});
  });
  h.frame();

  TrackedState& innerState = trackedState(h.rootElement(), Key::of("i"));

  // Dirty the leaf, then an ancestor whose own rebuild reaches it through
  // updateChild. The leaf must be built once, not twice.
  h.buildOwner().resetBuildCount();
  innerState.bump();
  root.rebuild();
  h.frame();

  CHECK_EQ(inner.builds, 2);
  CHECK_EQ(outer.builds, 2);
  CHECK_EQ(h.buildOwner().buildCount(), 1);
}

TEST(widgets_a_wrapper_rebuilding_alone_leaves_the_child_it_stored_untouched) {
  Harness h;
  Life outer;
  Life inner;
  int frameBuilds = 0;

  ScriptedRoot root(h, [&] {
    return Frame::make({
        .padding = EdgeInsets::all(2),
        .buildCount = &frameBuilds,
        .child = Column::make({.children = {
                                   Tracked::make({.key = Key::of("o"), .life = &outer}),
                                   Tracked::make({.key = Key::of("i"), .life = &inner}),
                               }}),
    });
  });
  h.frame();
  CHECK_EQ(frameBuilds, 1);

  // The Frame re-emits the child it adopted, which by now belongs to a released
  // build. That ref is the configuration the subtree already holds, so the
  // rebuild stops at the Frame instead of walking into it.
  elementFor(h.rootElement(), widgetTypeOf<Frame>()).markNeedsBuild();
  h.frame();

  CHECK_EQ(frameBuilds, 2);
  CHECK_EQ(outer.builds, 1);
  CHECK_EQ(inner.builds, 1);
  CHECK_EQ(outer.updates, 0);
}

TEST(widgets_unmounting_an_element_purges_it_from_the_dirty_list) {
  Harness h;
  Life life;
  bool present = true;

  ScriptedRoot root(h, [&] {
    return Column::make(
        {.children = {present ? Tracked::make({.life = &life}) : WidgetRef{}}});
  });
  h.frame();

  trackedState(h.rootElement()).bump();
  CHECK_EQ(h.buildOwner().dirtyElementCount(), std::size_t{1});

  // The rebuild that drops the child runs in the same flush that would have
  // rebuilt it. A stale entry here would be a dereference of a freed element.
  present = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(life.disposes, 1);
  CHECK_EQ(h.buildOwner().dirtyElementCount(), std::size_t{0});
  CHECK(!h.binding().needsFrame());
}

TEST(widgets_discarding_a_subtree_disposes_every_state_in_it) {
  Harness h;
  Life outer;
  Life inner;
  bool present = true;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             present ? Frame::make({
                                           .padding = EdgeInsets::all(2),
                                           .child = Column::make({.children = {
                                                                      Tracked::make({.life = &outer}),
                                                                      Tracked::make({.life = &inner}),
                                                                  }}),
                                       })
                                     : WidgetRef{},
                         }});
  });
  h.frame();
  CHECK_EQ(outer.inits, 1);
  CHECK_EQ(inner.inits, 1);

  present = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(outer.disposes, 1);
  CHECK_EQ(inner.disposes, 1);
  CHECK_EQ(flexIn(h.view()).childCount(), std::size_t{0});
}

TEST(widgets_setState_after_unmount_is_trapped_rather_than_crashing) {
  Harness h;
  Life life;
  h.attach([&] { return Tracked::make({.life = &life}); });
  h.frame();

  TrackedState& state = trackedState(h.rootElement());
  h.rootElement().unmount();

  CHECK(!state.mounted());
  CHECK_THROWS(state.bump());
}

TEST(widgets_a_build_that_never_settles_is_capped_rather_than_spinning) {
  Harness h;
  h.attach([] { return Restless::make({}); });

  CHECK_THROWS(h.frame());
}

TEST(widgets_the_build_arena_is_reused_rather_than_regrown) {
  Harness h;
  Life life;
  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             Tracked::make({.life = &life}),
                             SizedBox::make({.size = {4, 4}}),
                         }});
  });
  h.frame();

  const std::size_t chunks = h.buildOwner().arena().chunkCount();
  const std::size_t reserved = h.buildOwner().arena().reservedBytes();
  for (int i = 0; i < 32; ++i) {
    root.rebuild();
    h.frame();
  }

  CHECK_EQ(h.buildOwner().arena().chunkCount(), chunks);
  CHECK_EQ(h.buildOwner().arena().reservedBytes(), reserved);
  CHECK_EQ(h.buildOwner().arena().bytesUsed(), std::size_t{0});
}

TEST(widgets_a_widget_from_a_released_build_is_trapped) {
  MonospaceTextService text;
  PointerBinding pointers;
  KeyboardBinding keyboard;
  TickerRegistry tickers;
  BuildOwner owner(text, pointers, keyboard, tickers);

  WidgetRef escaped;
  {
    BuildScope scope(owner.arena());
    escaped = SizedBox::make({.size = {4, 4}});
    CHECK(escaped->name() == std::string("SizedBox"));
  }

  BuildScope next(owner.arena());
  CHECK_THROWS(escaped->name());
}

TEST(widgets_creating_a_widget_outside_a_build_scope_is_trapped) {
  CHECK_THROWS(SizedBox::make({.size = {4, 4}}));
}

TEST(widgets_a_repaint_boundary_widget_isolates_its_subtree) {
  Harness h({200, 100});
  Life outer;
  Life inner;
  Color innerColor = Color::argb(0xFF111111);

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {
                             Tracked::make({.key = Key::of("o"), .life = &outer}),
                             RepaintBoundary::make({
                                 .child = Tracked::make({.key = Key::of("i"),
                                                         .life = &inner,
                                                         .color = innerColor}),
                             }),
                         }});
  });
  const Scene before = h.frame();
  const std::uint64_t rootListRevision = before.root->revision();
  const std::uint64_t sceneRevision = before.revision;

  innerColor = Color::argb(0xFF222222);
  root.rebuild();
  const Scene after = h.frame();

  // Exactly one boundary re-recorded, and it was not the root's list.
  CHECK_EQ(h.pipeline().stats().boundariesRepainted, 1);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(after.root->revision(), rootListRevision);
  CHECK_NE(after.revision, sceneRevision);
}

TEST(widgets_element_tree_dump_is_stable) {
  Harness h;
  h.attach([] {
    return Column::make({.children = {
                             Text::make({.key = Key::of("hp"), .text = "HP"}),
                             Flexible::make({.flex = 1, .child = SizedBox::make({.size = {0, 4}})}),
                             Sprite::make({.key = Key::of(7), .size = {8, 8}}),
                         }});
  });
  h.frame();

  CHECK_EQ(dumpElementTree(h.rootElement()),
           "View\n"
           "  Column\n"
           "    Text key=\"hp\"\n"
           "    Flexible\n"
           "      SizedBox\n"
           "    Sprite key=7\n");
}
