#include "testing.hpp"

#include "fltr/harness.hpp"
#include "fltr/render/boxes.hpp"
#include "fltr/render/flex.hpp"
#include "fltr/render/stack.hpp"

using namespace fltr;

namespace {

/// A leaf of exactly the requested size.
std::unique_ptr<RenderConstrainedBox> fixed(float w, float h) {
  return std::make_unique<RenderConstrainedBox>(BoxConstraints::tightFor(w, h));
}

template <class T, class... A>
std::unique_ptr<T> make(A&&... a) {
  return std::make_unique<T>(std::forward<A>(a)...);
}

/// Lay out a detached root by hand. No pipeline, no widgets -- the render tree
/// has to be correct on its own before anything is built on top of it.
void layoutRoot(RenderBox& root, BoxConstraints c) { root.layout(c); }

std::string paintToString(RenderBox& root) {
  DisplayList list;
  list.beginRecording();
  PaintingContext ctx(list);
  root.paintWithContext(ctx, Offset::zero());
  list.endRecording();
  return dumpDisplayList(list);
}

}  // namespace

// ---------------------------------------------------------------------------
// Single-child layout
// ---------------------------------------------------------------------------

TEST(layout_padding_deflates_then_reinflates) {
  auto padding = make<RenderPadding>(EdgeInsets::only(10, 4, 6, 8));
  auto* leaf = fixed(50, 20).release();
  padding->setChild(std::unique_ptr<RenderBox>(leaf));

  layoutRoot(*padding, BoxConstraints{0, 200, 0, 200});

  CHECK_EQ(leaf->size(), (Size{50, 20}));
  CHECK_EQ(padding->size(), (Size{66, 32}));  // 50+10+6, 20+4+8
  CHECK_EQ(padding->childOffset(), (Offset{10, 4}));
}

TEST(layout_padding_larger_than_constraints_clamps_child_to_zero) {
  auto padding = make<RenderPadding>(EdgeInsets::all(30));
  padding->setChild(fixed(50, 50));
  layoutRoot(*padding, BoxConstraints::tight({40, 40}));
  // The deflated constraints are empty, so the child gets nothing and the
  // padding still reports the size its own constraints force.
  CHECK_EQ(padding->size(), (Size{40, 40}));
  CHECK_EQ(padding->child()->size(), (Size{0, 0}));
}

TEST(layout_align_expands_under_bounded_constraints) {
  auto align = make<RenderPositionedBox>(Alignment::bottomRight());
  auto* leaf = fixed(20, 10).release();
  align->setChild(std::unique_ptr<RenderBox>(leaf));

  layoutRoot(*align, BoxConstraints{0, 100, 0, 50});

  CHECK_EQ(align->size(), (Size{100, 50}));
  CHECK_EQ(align->childOffset(), (Offset{80, 40}));
}

TEST(layout_align_with_size_factor_shrinks_to_a_multiple_of_the_child) {
  auto align = make<RenderPositionedBox>(Alignment::center(), 2.0f, 3.0f);
  align->setChild(fixed(20, 10));
  layoutRoot(*align, BoxConstraints{0, 500, 0, 500});
  CHECK_EQ(align->size(), (Size{40, 30}));
  CHECK_EQ(align->childOffset(), (Offset{10, 10}));
}

TEST(layout_align_shrink_wraps_under_unbounded_constraints) {
  auto align = make<RenderPositionedBox>(Alignment::center());
  align->setChild(fixed(20, 10));
  layoutRoot(*align, BoxConstraints::unbounded());
  CHECK_EQ(align->size(), (Size{20, 10}));
}

TEST(layout_constrained_box_enforces_within_incoming_constraints) {
  auto box = make<RenderConstrainedBox>(BoxConstraints{0, 500, 0, 500});
  box->setChild(fixed(1000, 1000));
  // The additional constraints cannot escape the incoming ones.
  layoutRoot(*box, BoxConstraints{0, 100, 0, 80});
  CHECK_EQ(box->size(), (Size{100, 80}));
}

// ---------------------------------------------------------------------------
// Flex
// ---------------------------------------------------------------------------

TEST(flex_distributes_free_space_in_proportion_to_flex_factors) {
  auto row = make<RenderFlex>(Axis::Horizontal);
  row->addChild(fixed(50, 20));                    // inflexible
  row->addChild(fixed(10, 20), FlexChildData{1});  // flex 1
  row->addChild(fixed(10, 30), FlexChildData{2});  // flex 2

  layoutRoot(*row, BoxConstraints::tight({300, 100}));

  // Free space = 300 - 50 = 250, split 1:2 => 83.333 and 166.667.
  CHECK_EQ(row->size(), (Size{300, 100}));
  CHECK_NEAR(row->childAt(1).size().width, 250.0f / 3.0f, 1e-3);
  CHECK_NEAR(row->childAt(2).size().width, 500.0f / 3.0f, 1e-3);
  CHECK_EQ(row->childOffsetAt(0), (Offset{0, 40}));  // centred: (100-20)/2
  CHECK_NEAR(row->childOffsetAt(1).dx, 50.0f, 1e-3);
  CHECK_NEAR(row->childOffsetAt(2).dx, 50.0f + 250.0f / 3.0f, 1e-3);

  // The children exactly fill the row: no sub-pixel gap from rounding.
  const float end = row->childOffsetAt(2).dx + row->childAt(2).size().width;
  CHECK_NEAR(end, 300.0f, 1e-3);
}

TEST(flex_loose_fit_lets_a_flexible_child_take_less_than_its_share) {
  auto row = make<RenderFlex>(Axis::Horizontal);
  row->addChild(fixed(10, 10), FlexChildData{1, FlexFit::Tight});
  row->addChild(fixed(20, 10), FlexChildData{1, FlexFit::Loose});

  layoutRoot(*row, BoxConstraints::tight({200, 50}));

  CHECK_EQ(row->childAt(0).size().width, 100.0f);  // tight: fills its share
  CHECK_EQ(row->childAt(1).size().width, 20.0f);   // loose: takes only what it needs
}

TEST(flex_last_flexible_child_absorbs_slack_left_by_a_loose_sibling) {
  // Ordering matters here, and matching Flutter matters more than being tidy:
  // the last flexible child is given (freeSpace - alreadyAllocatedFlexSpace),
  // not its nominal share. So a loose sibling earlier in the list hands its
  // unused space to the last flexible child rather than leaving a gap.
  auto row = make<RenderFlex>(Axis::Horizontal);
  row->addChild(fixed(20, 10), FlexChildData{1, FlexFit::Loose});
  row->addChild(fixed(10, 10), FlexChildData{1, FlexFit::Tight});

  layoutRoot(*row, BoxConstraints::tight({200, 50}));

  CHECK_EQ(row->childAt(0).size().width, 20.0f);
  CHECK_EQ(row->childAt(1).size().width, 180.0f);
  CHECK_EQ(row->childOffsetAt(1).dx, 20.0f);
}

TEST(flex_cross_axis_alignment) {
  struct Case {
    CrossAxisAlignment align;
    float expectedOffsetY;
    float expectedHeight;
  };
  const Case cases[]{
      {CrossAxisAlignment::Start, 0.0f, 20.0f},
      {CrossAxisAlignment::End, 80.0f, 20.0f},
      {CrossAxisAlignment::Center, 40.0f, 20.0f},
      {CrossAxisAlignment::Stretch, 0.0f, 100.0f},
  };
  for (const Case& c : cases) {
    auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::Start, c.align);
    row->addChild(fixed(50, 20));
    layoutRoot(*row, BoxConstraints::tight({300, 100}));
    CHECK_EQ(row->childOffsetAt(0).dy, c.expectedOffsetY);
    CHECK_EQ(row->childAt(0).size().height, c.expectedHeight);
  }
}

TEST(flex_main_axis_alignment) {
  struct Case {
    MainAxisAlignment align;
    float first;
    float second;
  };
  // Two 50-wide children in a 300-wide row => 200 free.
  const Case cases[]{
      {MainAxisAlignment::Start, 0.0f, 50.0f},
      {MainAxisAlignment::End, 200.0f, 250.0f},
      {MainAxisAlignment::Center, 100.0f, 150.0f},
      {MainAxisAlignment::SpaceBetween, 0.0f, 250.0f},
      {MainAxisAlignment::SpaceAround, 50.0f, 200.0f},
      {MainAxisAlignment::SpaceEvenly, 200.0f / 3.0f, 250.0f / 3.0f + 50.0f + 200.0f / 3.0f - 200.0f / 3.0f},
  };
  for (const Case& c : cases) {
    auto row = make<RenderFlex>(Axis::Horizontal, c.align);
    row->addChild(fixed(50, 20));
    row->addChild(fixed(50, 20));
    layoutRoot(*row, BoxConstraints::tight({300, 100}));
    CHECK_NEAR(row->childOffsetAt(0).dx, c.first, 1e-3);
    if (c.align != MainAxisAlignment::SpaceEvenly) {
      CHECK_NEAR(row->childOffsetAt(1).dx, c.second, 1e-3);
    }
  }
  // SpaceEvenly: three equal gaps of 200/3 around two 50-wide children.
  auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::SpaceEvenly);
  row->addChild(fixed(50, 20));
  row->addChild(fixed(50, 20));
  layoutRoot(*row, BoxConstraints::tight({300, 100}));
  CHECK_NEAR(row->childOffsetAt(0).dx, 200.0f / 3.0f, 1e-3);
  CHECK_NEAR(row->childOffsetAt(1).dx, 2.0f * 200.0f / 3.0f + 50.0f, 1e-3);
}

TEST(flex_spacing_is_subtracted_before_distribution) {
  auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::Start,
                              CrossAxisAlignment::Center, MainAxisSize::Max, 10.0f);
  row->addChild(fixed(10, 10), FlexChildData{1});
  row->addChild(fixed(10, 10), FlexChildData{1});
  layoutRoot(*row, BoxConstraints::tight({210, 50}));
  // 210 - 10 spacing = 200, split evenly.
  CHECK_EQ(row->childAt(0).size().width, 100.0f);
  CHECK_EQ(row->childAt(1).size().width, 100.0f);
  CHECK_EQ(row->childOffsetAt(1).dx, 110.0f);
}

TEST(flex_main_axis_size_min_shrink_wraps) {
  auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::Start,
                              CrossAxisAlignment::Center, MainAxisSize::Min);
  row->addChild(fixed(30, 10));
  row->addChild(fixed(20, 10));
  layoutRoot(*row, BoxConstraints{0, 300, 0, 100});
  CHECK_EQ(row->size().width, 50.0f);
}

TEST(flex_column_swaps_the_axes) {
  auto column = make<RenderFlex>(Axis::Vertical, MainAxisAlignment::Start,
                                 CrossAxisAlignment::Start);
  column->addChild(fixed(30, 10));
  column->addChild(fixed(20, 40));
  layoutRoot(*column, BoxConstraints::tight({100, 200}));
  CHECK_EQ(column->childOffsetAt(0), (Offset{0, 0}));
  CHECK_EQ(column->childOffsetAt(1), (Offset{0, 10}));
  CHECK_EQ(column->size(), (Size{100, 200}));
}

TEST(flex_unbounded_main_axis_treats_flexible_children_as_inflexible) {
  auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::Start,
                              CrossAxisAlignment::Start, MainAxisSize::Min);
  row->addChild(fixed(40, 10), FlexChildData{1});
  layoutRoot(*row, BoxConstraints::unbounded());
  CHECK_EQ(row->childAt(0).size().width, 40.0f);
  CHECK_EQ(row->size().width, 40.0f);
}

// ---------------------------------------------------------------------------
// Stack
// ---------------------------------------------------------------------------

TEST(stack_sizes_to_its_largest_non_positioned_child) {
  auto stack = make<RenderStack>(Alignment::center(), StackFit::Loose);
  stack->addChild(fixed(40, 20));
  stack->addChild(fixed(10, 60));
  layoutRoot(*stack, BoxConstraints{0, 500, 0, 500});
  CHECK_EQ(stack->size(), (Size{40, 60}));
  CHECK_EQ(stack->childOffsetAt(0), (Offset{0, 20}));   // centred
  CHECK_EQ(stack->childOffsetAt(1), (Offset{15, 0}));
}

TEST(stack_positioned_children_do_not_affect_the_stack_size) {
  auto stack = make<RenderStack>(Alignment::topLeft(), StackFit::Loose);
  stack->addChild(fixed(40, 20));
  StackChildData pos;
  pos.positioned = true;
  pos.left = 5.0f;
  pos.top = 7.0f;
  pos.width = 500.0f;
  pos.height = 500.0f;
  stack->addChild(fixed(1, 1), pos);

  layoutRoot(*stack, BoxConstraints{0, 200, 0, 200});
  CHECK_EQ(stack->size(), (Size{40, 20}));
  CHECK_EQ(stack->childOffsetAt(1), (Offset{5, 7}));
}

TEST(stack_positioned_with_opposing_edges_is_stretched) {
  auto stack = make<RenderStack>();
  stack->addChild(fixed(100, 100));
  StackChildData pos;
  pos.positioned = true;
  pos.left = 10.0f;
  pos.right = 20.0f;
  pos.top = 5.0f;
  pos.bottom = 5.0f;
  stack->addChild(fixed(1, 1), pos);

  layoutRoot(*stack, BoxConstraints::tight({100, 100}));
  CHECK_EQ(stack->childAt(1).size(), (Size{70, 90}));
  CHECK_EQ(stack->childOffsetAt(1), (Offset{10, 5}));
}

TEST(stack_positioned_from_the_far_edge) {
  auto stack = make<RenderStack>();
  stack->addChild(fixed(100, 100));
  StackChildData pos;
  pos.positioned = true;
  pos.right = 10.0f;
  pos.bottom = 20.0f;
  pos.width = 30.0f;
  pos.height = 40.0f;
  stack->addChild(fixed(1, 1), pos);

  layoutRoot(*stack, BoxConstraints::tight({100, 100}));
  CHECK_EQ(stack->childOffsetAt(1), (Offset{60, 40}));  // 100-10-30, 100-20-40
}

TEST(stack_expand_fit_is_sized_by_parent) {
  auto stack = make<RenderStack>(Alignment::topLeft(), StackFit::Expand);
  stack->addChild(fixed(10, 10));
  CHECK(stack->sizedByParent());
  layoutRoot(*stack, BoxConstraints{0, 120, 0, 90});
  CHECK_EQ(stack->size(), (Size{120, 90}));
  // Expand forces children to the stack's size.
  CHECK_EQ(stack->childAt(0).size(), (Size{120, 90}));
}

TEST(stack_expand_under_unbounded_constraints_is_a_contract_violation) {
  auto stack = make<RenderStack>(Alignment::topLeft(), StackFit::Expand);
  stack->addChild(fixed(10, 10));
  CHECK_THROWS(layoutRoot(*stack, BoxConstraints::unbounded()));
}

// ---------------------------------------------------------------------------
// Contract enforcement
// ---------------------------------------------------------------------------

namespace {
/// Deliberately reports a size its constraints forbid.
class BadBox final : public RenderBox {
public:
  const char* typeName() const override { return "BadBox"; }
  void performLayout() override { setSize({1000, 1000}); }
};
}  // namespace

TEST(layout_a_size_violating_constraints_is_a_contract_violation) {
  auto bad = make<BadBox>();
  CHECK_THROWS(layoutRoot(*bad, BoxConstraints::tight({10, 10})));
}

TEST(layout_non_normalized_constraints_are_rejected) {
  auto box = fixed(10, 10);
  CHECK_THROWS(box->layout(BoxConstraints{100, 10, 0, 10}));
}

// ---------------------------------------------------------------------------
// Text participates in layout as an ordinary child
// ---------------------------------------------------------------------------

TEST(paragraph_resolves_its_size_from_constraints) {
  MonospaceTextService text(0.5f);
  auto para = make<RenderParagraph>(&text, "alpha beta gamma", TextStyle{.size = 10.0f});

  // Wide: one line.
  layoutRoot(*para, BoxConstraints{0, 500, 0, 500});
  CHECK_EQ(para->size(), (Size{80, 12}));

  // Narrow: the same widget wraps, and layout picks it up with no special case.
  para->layout(BoxConstraints{0, 60, 0, 500});
  CHECK_EQ(para->size().height, 24.0f);
  CHECK(para->size().width <= 60.0f);
}

TEST(paragraph_inside_padding_inside_a_row) {
  MonospaceTextService text(0.5f);
  auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::Start,
                              CrossAxisAlignment::Start, MainAxisSize::Min);
  auto padding = make<RenderPadding>(EdgeInsets::all(4));
  padding->setChild(make<RenderParagraph>(&text, "hi", TextStyle{.size = 10.0f}));
  row->addChild(std::move(padding));

  layoutRoot(*row, BoxConstraints{0, 500, 0, 500});
  // "hi" is 2 chars * 5px = 10 wide, 12 tall; plus 4px padding on each side.
  CHECK_EQ(row->size(), (Size{18, 20}));
}

TEST(paragraph_releases_its_handle_on_destruction) {
  MonospaceTextService text;
  {
    auto para = make<RenderParagraph>(&text, "x", TextStyle{});
    layoutRoot(*para, BoxConstraints{0, 100, 0, 100});
    CHECK_EQ(text.liveParagraphs(), std::size_t{1});
  }
  CHECK_EQ(text.liveParagraphs(), std::size_t{0});
}

TEST(paragraph_relayout_does_not_leak_handles) {
  MonospaceTextService text;
  auto para = make<RenderParagraph>(&text, "hello world", TextStyle{.size = 10.0f});
  for (int i = 0; i < 20; ++i) {
    para->setText(i % 2 ? "hello world" : "goodbye world");
    para->layout(BoxConstraints{0, 40.0f + static_cast<float>(i), 0, 500});
    CHECK_EQ(text.liveParagraphs(), std::size_t{1});
  }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

TEST(paint_decoration_then_child_in_order) {
  auto decorated = make<RenderDecoratedBox>(
      BoxDecoration{.color = Color::argb(0xFF102030), .radius = BorderRadius::all(4)});
  auto inner = make<RenderDecoratedBox>(BoxDecoration{.color = Color::argb(0xFFAABBCC)});
  inner->setChild(fixed(10, 10));
  auto padding = make<RenderPadding>(EdgeInsets::all(5));
  padding->setChild(std::move(inner));
  decorated->setChild(std::move(padding));

  layoutRoot(*decorated, BoxConstraints{0, 100, 0, 100});
  CHECK_EQ(decorated->size(), (Size{20, 20}));

  CHECK_EQ(paintToString(*decorated),
           "DrawRRect [0,0 20x20] #FF102030 r=4\n"
           "DrawRRect [5,5 10x10] #FFAABBCC\n");
}

TEST(paint_opacity_wraps_its_subtree) {
  auto opacity = make<RenderOpacity>(0.25f);
  auto box = make<RenderDecoratedBox>(BoxDecoration{.color = Color::argb(0xFFFF0000)});
  box->setChild(fixed(10, 10));
  opacity->setChild(std::move(box));
  layoutRoot(*opacity, BoxConstraints{0, 100, 0, 100});

  CHECK_EQ(paintToString(*opacity),
           "PushOpacity 0.25\n"
           "  DrawRRect [0,0 10x10] #FFFF0000\n"
           "Pop\n");
}

TEST(paint_fully_opaque_records_no_push_and_fully_transparent_records_nothing) {
  auto opaque = make<RenderOpacity>(1.0f);
  auto box = make<RenderDecoratedBox>(BoxDecoration{.color = Color::argb(0xFFFF0000)});
  box->setChild(fixed(10, 10));
  opaque->setChild(std::move(box));
  layoutRoot(*opaque, BoxConstraints{0, 100, 0, 100});
  CHECK_EQ(paintToString(*opaque), "DrawRRect [0,0 10x10] #FFFF0000\n");

  opaque->setOpacity(0.0f);
  CHECK_EQ(paintToString(*opaque), "");
}

TEST(paint_transform_is_applied_about_its_origin) {
  auto xf = make<RenderTransform>(Transform2D::scaling(2, 2), Alignment::center());
  auto box = make<RenderDecoratedBox>(BoxDecoration{.color = Color::argb(0xFF00FF00)});
  box->setChild(fixed(10, 20));
  xf->setChild(std::move(box));
  layoutRoot(*xf, BoxConstraints{0, 100, 0, 100});

  // Scale 2x about (5,10): [a c tx; b d ty] = [2 0 -5; 0 2 -10]
  CHECK_EQ(paintToString(*xf),
           "PushTransform [2 0 -5; 0 2 -10]\n"
           "  DrawRRect [0,0 10x20] #FF00FF00\n"
           "Pop\n");
}

TEST(paint_sprite_refers_to_a_consumer_owned_handle) {
  auto sprite = make<RenderSprite>(ImageHandle{42}, Size{16, 16},
                                   Rect::fromLTWH(0, 0, 32, 32));
  layoutRoot(*sprite, BoxConstraints{0, 100, 0, 100});
  CHECK_EQ(paintToString(*sprite), "DrawImage [0,0 16x16] image=42\n");
}

TEST(paint_clip_wraps_its_child) {
  auto clip = make<RenderClipRect>(BorderRadius::all(3));
  auto box = make<RenderDecoratedBox>(BoxDecoration{.color = Color::argb(0xFF010203)});
  box->setChild(fixed(30, 30));
  clip->setChild(std::move(box));
  layoutRoot(*clip, BoxConstraints{0, 100, 0, 100});
  CHECK_EQ(paintToString(*clip),
           "PushClipRect [0,0 30x30] r=3\n"
           "  DrawRRect [0,0 30x30] #FF010203\n"
           "Pop\n");
}

// ---------------------------------------------------------------------------
// Tree dump
// ---------------------------------------------------------------------------

TEST(render_tree_dump_is_stable_and_readable) {
  auto view = make<RenderView>(Size{200, 100});
  auto padding = make<RenderPadding>(EdgeInsets::all(8));
  auto row = make<RenderFlex>(Axis::Horizontal, MainAxisAlignment::SpaceBetween,
                              CrossAxisAlignment::Center);
  row->addChild(fixed(40, 20));
  row->addChild(fixed(30, 10));
  padding->setChild(std::move(row));
  view->setChild(std::move(padding));

  layoutRoot(*view, BoxConstraints::tight({200, 100}));
  DisplayList list;
  list.beginRecording();
  PaintingContext ctx(list);
  view->paintWithContext(ctx, Offset::zero());
  list.endRecording();

  // Note which nodes are relayout boundaries and which are not. The View lays
  // out the Padding with parentUsesSize = false, so the Padding is a boundary
  // and nothing below it can dirty the View. The Row *measures* its children,
  // so they are not boundaries and their invalidations do reach the Row.
  CHECK_EQ(dumpRenderTree(*view),
           "View size=200x100 [repaint-boundary] [relayout-boundary]\n"
           "  Padding size=200x100 pad=all(8) [relayout-boundary]\n"
           "    Row size=184x84 at=(8,8) main=between cross=center [relayout-boundary]\n"
           "      ConstrainedBox size=40x20 at=(0,32) extra=w[40..40] h[20..20]\n"
           "      ConstrainedBox size=30x10 at=(154,37) extra=w[30..30] h[10..10]\n");
}

// ---------------------------------------------------------------------------
// Hit-test geometry
//
// Routing, the arena, and recognizers are M5. What the render tree owns is
// resolving which boxes are under a point and in what space, which is what
// these cover.
// ---------------------------------------------------------------------------

namespace {

/// A leaf that accepts hits, so a path has something to terminate at.
class RenderOpaque final : public RenderBox {
public:
  explicit RenderOpaque(Size preferred) : preferred_(preferred) {}
  const char* typeName() const override { return "Opaque"; }
  bool hitTestSelf(Offset) const override { return true; }
  void performLayout() override { setSize(constraints_.constrain(preferred_)); }

private:
  Size preferred_;
};

std::size_t hitCount(RenderBox& root, Offset at) {
  HitTestResult result;
  root.hitTest(result, at);
  return result.path().size();
}

}  // namespace

TEST(hit_test_misses_outside_the_box_and_on_the_exclusive_edges) {
  auto box = make<RenderOpaque>(Size{20, 10});
  box->layout(BoxConstraints::tight({20, 10}));

  CHECK_EQ(hitCount(*box, {0, 0}), std::size_t{1});
  CHECK_EQ(hitCount(*box, {19.9f, 9.9f}), std::size_t{1});
  CHECK_EQ(hitCount(*box, {20, 5}), std::size_t{0});
  CHECK_EQ(hitCount(*box, {5, 10}), std::size_t{0});
  CHECK_EQ(hitCount(*box, {-1, 5}), std::size_t{0});
}

TEST(hit_test_reports_the_path_deepest_first_and_in_local_space) {
  auto padding = make<RenderPadding>(EdgeInsets::only(8, 4, 0, 0));
  padding->setChild(make<RenderOpaque>(Size{20, 10}));
  padding->layout(BoxConstraints::loose({100, 100}));

  HitTestResult result;
  CHECK(padding->hitTest(result, {10, 6}));
  CHECK_EQ(result.path().size(), std::size_t{2});
  CHECK_EQ(std::string(result.path()[0].target->typeName()), std::string("Opaque"));
  CHECK_EQ(std::string(result.path()[1].target->typeName()), std::string("Padding"));
  // The shift is undone on the way down, so the leaf sees its own origin.
  CHECK_EQ(result.path()[0].localPosition, (Offset{2, 2}));
  CHECK_EQ(result.path()[1].localPosition, (Offset{10, 6}));
}

TEST(hit_test_inverts_a_transform_on_the_way_down) {
  auto transform = make<RenderTransform>(Transform2D::scaling(2, 2), Alignment::topLeft());
  transform->setChild(make<RenderOpaque>(Size{20, 10}));
  transform->layout(BoxConstraints::tight({20, 10}));

  HitTestResult result;
  // Painted at 2x about the top-left, so (10,6) on screen is (5,3) to the child.
  CHECK(transform->hitTest(result, {10, 6}));
  CHECK_EQ(result.path()[0].localPosition, (Offset{5, 3}));

  // A degenerate transform is not invertible, so the walk refuses to recurse
  // rather than guessing.
  auto collapsed = make<RenderTransform>(Transform2D::scaling(0, 0), Alignment::topLeft());
  collapsed->setChild(make<RenderOpaque>(Size{20, 10}));
  collapsed->layout(BoxConstraints::tight({20, 10}));
  CHECK_EQ(hitCount(*collapsed, {5, 5}), std::size_t{0});
}

TEST(hit_test_resolves_overlapping_children_topmost_first) {
  auto stack = make<RenderStack>(Alignment::topLeft(), StackFit::Expand);
  stack->addChild(make<RenderOpaque>(Size{50, 50}));
  stack->addChild(make<RenderOpaque>(Size{50, 50}),
                  {.positioned = true, .left = 10.0f, .top = 0.0f});
  layoutRoot(*stack, BoxConstraints::tight({50, 50}));

  // Later children paint on top, so the second one wins where they overlap.
  HitTestResult overlap;
  CHECK(stack->hitTest(overlap, {20, 20}));
  CHECK_EQ(overlap.path()[0].localPosition, (Offset{10, 20}));

  // Left of the positioned child, only the first one is there.
  HitTestResult single;
  CHECK(stack->hitTest(single, {5, 20}));
  CHECK_EQ(single.path()[0].localPosition, (Offset{5, 20}));
}
