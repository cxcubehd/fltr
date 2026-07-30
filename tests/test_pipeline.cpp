#include "testing.hpp"

#include "fltr/harness.hpp"
#include "fltr/render/boxes.hpp"
#include "fltr/render/flex.hpp"
#include "fltr/render/stack.hpp"

using namespace fltr;

namespace {

template <class T, class... A>
std::unique_ptr<T> make(A&&... a) {
  return std::make_unique<T>(std::forward<A>(a)...);
}

/// A leaf that paints something, so recorded output is observable, and whose
/// size can be changed to dirty layout from the bottom of the tree.
class RenderProbe final : public RenderBox {
public:
  explicit RenderProbe(Size preferred, Color color = Color::argb(0xFF102030))
      : preferred_(preferred), color_(color) {}

  const char* typeName() const override { return "Probe"; }

  void setPreferredSize(Size s) {
    if (s == preferred_) return;
    preferred_ = s;
    markNeedsLayout();
  }
  void setColor(Color c) {
    if (c == color_) return;
    color_ = c;
    markNeedsPaint();  // the cheap path: no layout may run because of this
  }

  /// Exposes the render-attached observation seam. This is the path an animated
  /// render object takes in M7, and it is already load-bearing here.
  void repaintOn(Listenable* source) { observeForPaint(paintSub_, source); }

  void performLayout() override { setSize(constraints_.constrain(preferred_)); }
  void paint(PaintingContext& context, Offset offset) override {
    context.list().drawRect(Rect::fromOriginSize(offset, size_), color_);
  }

private:
  Size preferred_;
  Color color_;
  Subscription paintSub_;
};

/// Deliberately illegal: dirties layout from inside its own paint.
class RenderIllegalPainter final : public RenderBox {
public:
  const char* typeName() const override { return "IllegalPainter"; }
  void performLayout() override { setSize(constraints_.constrain({10, 10})); }
  void paint(PaintingContext&, Offset) override { markNeedsLayout(); }
};

/// Deliberately illegal: re-enters the pipeline from inside its own layout.
class RenderReentrantLayout final : public RenderBox {
public:
  const char* typeName() const override { return "ReentrantLayout"; }
  void performLayout() override {
    setSize(constraints_.constrain({10, 10}));
    owner()->flushLayout();
  }
};

/// Deliberately non-converging: dirties a partner every time it lays out, so a
/// pair of them keeps re-dirtying each other. A node cannot spin on its own --
/// it is still marked dirty while its own performLayout runs, so marking itself
/// short-circuits -- which is why this takes two.
class RenderPingPong final : public RenderBox {
public:
  void setPartner(RenderBox* p) { partner_ = p; }
  const char* typeName() const override { return "PingPong"; }
  void performLayout() override {
    setSize(constraints_.constrain({10, 10}));
    if (partner_) partner_->markNeedsLayout();
  }

private:
  RenderBox* partner_ = nullptr;
};

struct Signal : Listenable {
  void fire() { notifyListeners(); }
};

/// View > Align > Padding > Row > [a, b], and which of those is a relayout
/// boundary is the whole point of the arrangement:
///   View     no parent                          -> boundary
///   Align    laid out with tight constraints     -> boundary
///   Padding  measured under loosened constraints -> NOT a boundary
///   Row      measured under loose constraints    -> NOT a boundary
///   a, b     measured under loose constraints    -> NOT a boundary
///
/// The Align is load-bearing. Padding deflates its constraints but does not
/// loosen them, so View > Padding would hand the Row *tight* constraints and
/// make it a boundary -- leaving no non-boundary chain to test propagation with.
struct BasicTree {
  PipelineOwner owner;
  std::unique_ptr<RenderView> view;
  RenderPositionedBox* align = nullptr;
  RenderPadding* padding = nullptr;
  RenderFlex* row = nullptr;
  RenderProbe* a = nullptr;
  RenderProbe* b = nullptr;

  BasicTree() {
    view = make<RenderView>(Size{200, 100});
    auto aligned = make<RenderPositionedBox>(Alignment::center());
    auto padded = make<RenderPadding>(EdgeInsets::all(8));
    auto flex = make<RenderFlex>(Axis::Horizontal);
    auto pa = make<RenderProbe>(Size{40, 20});
    auto pb = make<RenderProbe>(Size{60, 30});
    a = pa.get();
    b = pb.get();
    row = flex.get();
    padding = padded.get();
    align = aligned.get();
    row->addChild(std::move(pa));
    row->addChild(std::move(pb));
    padded->setChild(std::move(flex));
    aligned->setChild(std::move(padded));
    view->setChild(std::move(aligned));
    owner.setRootNode(view.get());
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Driving a frame
// ---------------------------------------------------------------------------

TEST(pipeline_first_frame_lays_out_and_paints_the_whole_tree) {
  BasicTree t;
  CHECK(t.owner.needsFrame());

  const Scene scene = t.owner.drawFrame();

  CHECK(!t.owner.needsFrame());
  CHECK_EQ(t.owner.stats().layoutPasses, 1);
  CHECK_EQ(t.owner.stats().paints, 6);  // view, align, padding, row, a, b
  CHECK_EQ(t.owner.stats().boundariesRelaidOut, 1);   // the root
  CHECK_EQ(t.owner.stats().boundariesRepainted, 1);   // the root
  CHECK_EQ(t.view->size(), (Size{200, 100}));
  CHECK_EQ(scene.surface, (Size{200, 100}));
  CHECK(scene.root != nullptr);
  CHECK(!scene.root->empty());
}

TEST(pipeline_a_frame_with_no_change_does_no_work_and_stays_resubmittable) {
  BasicTree t;
  const Scene first = t.owner.drawFrame();
  const std::string recorded = dumpDisplayList(*first.root);

  const Scene second = t.owner.drawFrame();

  // Zero work, by every measure the pipeline keeps.
  CHECK(!t.owner.needsFrame());
  CHECK_EQ(t.owner.stats().layouts, 0);
  CHECK_EQ(t.owner.stats().paints, 0);
  CHECK_EQ(t.owner.stats().layoutPasses, 0);
  CHECK_EQ(t.owner.stats().boundariesRelaidOut, 0);
  CHECK_EQ(t.owner.stats().boundariesRepainted, 0);

  // Unchanged revision is the consumer's signal that last frame's translation
  // is still valid, and the commands are still there to resubmit.
  CHECK_EQ(second.revision, first.revision);
  CHECK_EQ(second.root, first.root);
  CHECK_EQ(dumpDisplayList(*second.root), recorded);
}

TEST(pipeline_scene_revision_changes_exactly_when_something_was_rerecorded) {
  BasicTree t;
  const std::uint64_t r0 = t.owner.drawFrame().revision;
  const std::uint64_t r1 = t.owner.drawFrame().revision;
  CHECK_EQ(r1, r0);

  t.a->setColor(Color::argb(0xFFAA0000));
  const std::uint64_t r2 = t.owner.drawFrame().revision;
  CHECK_NE(r2, r1);

  const std::uint64_t r3 = t.owner.drawFrame().revision;
  CHECK_EQ(r3, r2);
}

TEST(pipeline_steady_state_frames_allocate_nothing) {
  BasicTree t;
  // Warm every buffer: the dirty-list scratch, each display list's command
  // storage, and the root's boundary list.
  for (int i = 0; i < 4; ++i) {
    t.a->setColor(Color::argb(0xFF000000u + static_cast<std::uint32_t>(i)));
    t.a->setPreferredSize({40.0f + static_cast<float>(i), 20});
    t.owner.drawFrame();
  }

  // A frame with nothing dirty.
  std::size_t before = fltrtest::allocationCount();
  t.owner.drawFrame();
  CHECK_EQ(fltrtest::allocationCount() - before, std::size_t{0});

  // A repaint-only frame, which is what an animation attached to a render
  // object produces every single tick.
  before = fltrtest::allocationCount();
  t.a->setColor(Color::argb(0xFF123456));
  t.owner.drawFrame();
  CHECK_EQ(fltrtest::allocationCount() - before, std::size_t{0});

  // And a relayout frame.
  before = fltrtest::allocationCount();
  t.a->setPreferredSize({44, 20});
  t.owner.drawFrame();
  CHECK_EQ(fltrtest::allocationCount() - before, std::size_t{0});
}

// ---------------------------------------------------------------------------
// Relayout boundaries: dirtying a leaf must relayout a bounded set
// ---------------------------------------------------------------------------

TEST(pipeline_dirtying_a_leaf_relayouts_only_up_to_the_nearest_boundary) {
  BasicTree t;
  t.owner.drawFrame();

  const std::uint32_t viewBefore = t.view->layoutCount();
  const std::uint32_t alignBefore = t.align->layoutCount();
  const std::uint32_t paddingBefore = t.padding->layoutCount();
  const std::uint32_t rowBefore = t.row->layoutCount();

  t.a->setPreferredSize({50, 20});
  // Three non-boundaries were walked through (a, row, padding) and exactly one
  // node was registered: the align, the nearest enclosing boundary.
  CHECK_EQ(t.owner.dirtyLayoutCount(), std::size_t{1});

  t.owner.drawFrame();

  CHECK_EQ(t.owner.stats().boundariesRelaidOut, 1);
  CHECK_EQ(t.view->layoutCount(), viewBefore);           // above the boundary: untouched
  CHECK_EQ(t.align->layoutCount(), alignBefore + 1);     // the boundary itself
  CHECK_EQ(t.padding->layoutCount(), paddingBefore + 1);
  CHECK_EQ(t.row->layoutCount(), rowBefore + 1);         // below it: re-laid out
  CHECK_EQ(t.a->size(), (Size{50, 20}));
}

TEST(pipeline_a_child_under_tight_constraints_is_its_own_boundary) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto sized = make<RenderConstrainedBox>(BoxConstraints::tightFor(40, 20));
  auto probe = make<RenderProbe>(Size{40, 20});
  RenderProbe* probeRaw = probe.get();
  RenderConstrainedBox* sizedRaw = sized.get();
  RenderFlex* rowRaw = row.get();
  sized->setChild(std::move(probe));
  row->addChild(std::move(sized));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());
  owner.drawFrame();

  const std::uint32_t rowBefore = rowRaw->layoutCount();
  const std::uint32_t sizedBefore = sizedRaw->layoutCount();

  // The constrained box hands its child a tight constraint, so no size the child
  // computes can ever be visible to anything above it.
  CHECK(probeRaw->isRelayoutBoundary());
  probeRaw->setPreferredSize({500, 500});
  owner.drawFrame();

  CHECK_EQ(owner.stats().boundariesRelaidOut, 1);
  CHECK_EQ(owner.stats().layouts, 1);  // exactly one node was laid out
  CHECK_EQ(rowRaw->layoutCount(), rowBefore);
  CHECK_EQ(sizedRaw->layoutCount(), sizedBefore);
  CHECK_EQ(probeRaw->size(), (Size{40, 20}));  // clamped by the tight constraint
}

TEST(pipeline_sizedByParent_stops_layout_propagation) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto stack = make<RenderStack>(Alignment::topLeft(), StackFit::Expand);
  auto probe = make<RenderProbe>(Size{40, 20});
  RenderProbe* probeRaw = probe.get();
  RenderStack* stackRaw = stack.get();
  stack->addChild(std::move(probe));
  view->setChild(std::move(stack));
  owner.setRootNode(view.get());
  owner.drawFrame();

  CHECK(stackRaw->sizedByParent());
  CHECK(stackRaw->isRelayoutBoundary());
  // Expand gives children tight constraints, so the child is a boundary too and
  // its own size changes cannot reach the stack.
  CHECK(probeRaw->isRelayoutBoundary());

  const std::uint32_t stackBefore = stackRaw->layoutCount();
  probeRaw->setPreferredSize({10, 10});
  owner.drawFrame();

  CHECK_EQ(stackRaw->layoutCount(), stackBefore);
  CHECK_EQ(probeRaw->size(), (Size{200, 100}));  // forced by Expand
}

TEST(pipeline_a_measured_child_under_loose_constraints_is_not_a_boundary) {
  BasicTree t;
  t.owner.drawFrame();

  // The negative case that makes the positive ones mean something: each of
  // these was measured by its parent under constraints that leave it room to
  // choose, so its size is visible upward and it cannot be a boundary.
  CHECK(!t.a->isRelayoutBoundary());
  CHECK(!t.b->isRelayoutBoundary());
  CHECK(!t.row->isRelayoutBoundary());
  CHECK(!t.padding->isRelayoutBoundary());
  // And the two that are: tight constraints, and no parent.
  CHECK(t.align->isRelayoutBoundary());
  CHECK(t.view->isRelayoutBoundary());
}

TEST(pipeline_reparenting_resets_the_boundary_decision) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto sized = make<RenderConstrainedBox>(BoxConstraints::tightFor(40, 20));
  auto probe = make<RenderProbe>(Size{40, 20});
  RenderProbe* probeRaw = probe.get();
  RenderConstrainedBox* sizedRaw = sized.get();
  RenderFlex* rowRaw = row.get();
  sized->setChild(std::move(probe));
  row->addChild(std::move(sized));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());
  owner.drawFrame();

  CHECK(probeRaw->isRelayoutBoundary());  // tight constraints from the sized box

  // Move it out from under the tight constraints and directly into the row,
  // which measures its children loosely.
  std::unique_ptr<RenderBox> moved = sizedRaw->takeChild();
  CHECK(!probeRaw->isRelayoutBoundary());  // reset to unknown on drop
  rowRaw->addChild(std::move(moved));
  owner.drawFrame();

  CHECK(!probeRaw->isRelayoutBoundary());  // and stays not-a-boundary in its new home

  const std::uint32_t rowBefore = rowRaw->layoutCount();
  probeRaw->setPreferredSize({70, 20});
  owner.drawFrame();
  // Now its size does reach the row, so the row must have been re-laid-out.
  CHECK_EQ(rowRaw->layoutCount(), rowBefore + 1);
  CHECK_EQ(probeRaw->size(), (Size{70, 20}));
}

// ---------------------------------------------------------------------------
// Paint invalidation is a separate, cheaper path
// ---------------------------------------------------------------------------

TEST(pipeline_a_paint_only_change_performs_no_layout) {
  BasicTree t;
  t.owner.drawFrame();

  t.a->setColor(Color::argb(0xFF00FF00));
  CHECK_EQ(t.owner.dirtyLayoutCount(), std::size_t{0});
  CHECK_EQ(t.owner.dirtyPaintCount(), std::size_t{1});

  const std::uint32_t layoutsBefore = t.a->layoutCount();
  t.owner.drawFrame();

  CHECK_EQ(t.owner.stats().layouts, 0);
  CHECK_EQ(t.owner.stats().layoutPasses, 0);
  CHECK_EQ(t.owner.stats().boundariesRepainted, 1);
  CHECK_EQ(t.a->layoutCount(), layoutsBefore);
}

TEST(pipeline_repainting_a_nested_boundary_leaves_the_root_list_alone) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto plain = make<RenderProbe>(Size{40, 20});
  auto isolated = make<RenderRepaintBoundary>();
  auto inner = make<RenderProbe>(Size{60, 30}, Color::argb(0xFF334455));
  RenderProbe* plainRaw = plain.get();
  RenderProbe* innerRaw = inner.get();
  RenderRepaintBoundary* boundary = isolated.get();
  isolated->setChild(std::move(inner));
  row->addChild(std::move(plain));
  row->addChild(std::move(isolated));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());

  const Scene scene = owner.drawFrame();
  const std::uint64_t rootRev = scene.root->revision();
  const std::uint64_t innerRev = boundary->boundaryListIfAny()->revision();
  const std::uint32_t plainPaints = plainRaw->paintCount();

  // A paint-only change inside the boundary.
  innerRaw->setColor(Color::argb(0xFFFF0000));
  const Scene after = owner.drawFrame();

  CHECK_EQ(owner.stats().layouts, 0);
  CHECK_EQ(owner.stats().boundariesRepainted, 1);
  // The root was not re-recorded; only the boundary's own list was.
  CHECK_EQ(after.root->revision(), rootRev);
  CHECK_NE(boundary->boundaryListIfAny()->revision(), innerRev);
  CHECK_EQ(plainRaw->paintCount(), plainPaints);  // the sibling never repainted
  // But the scene revision still moved, so the consumer knows to re-walk.
  CHECK_NE(after.revision, scene.revision);
}

TEST(pipeline_repainting_the_root_does_not_rerecord_a_clean_nested_boundary) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto plain = make<RenderProbe>(Size{40, 20});
  auto isolated = make<RenderRepaintBoundary>();
  auto inner = make<RenderProbe>(Size{60, 30});
  RenderProbe* plainRaw = plain.get();
  RenderProbe* innerRaw = inner.get();
  RenderRepaintBoundary* boundary = isolated.get();
  isolated->setChild(std::move(inner));
  row->addChild(std::move(plain));
  row->addChild(std::move(isolated));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());
  owner.drawFrame();

  const std::uint64_t innerRev = boundary->boundaryListIfAny()->revision();
  const std::uint32_t innerPaints = innerRaw->paintCount();

  // Dirty something outside the boundary; the root must re-record.
  plainRaw->setColor(Color::argb(0xFF0000FF));
  owner.drawFrame();

  CHECK_EQ(owner.stats().boundariesRepainted, 1);  // the root only
  CHECK_EQ(boundary->boundaryListIfAny()->revision(), innerRev);
  CHECK_EQ(innerRaw->paintCount(), innerPaints);
}

TEST(pipeline_moving_a_repaint_boundary_does_not_rerecord_it) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto stack = make<RenderStack>(Alignment::topLeft(), StackFit::Loose);
  auto isolated = make<RenderRepaintBoundary>();
  auto inner = make<RenderProbe>(Size{40, 20});
  RenderProbe* innerRaw = inner.get();
  RenderRepaintBoundary* boundary = isolated.get();
  RenderStack* stackRaw = stack.get();
  isolated->setChild(std::move(inner));
  stack->addChild(std::move(isolated));
  view->setChild(std::move(stack));
  owner.setRootNode(view.get());

  const Scene scene = owner.drawFrame();
  const std::uint64_t innerRev = boundary->boundaryListIfAny()->revision();
  // Captured by value: `scene.root` and `after.root` are the same list object.
  const std::uint64_t rootRev = scene.root->revision();
  const std::uint32_t innerPaints = innerRaw->paintCount();
  CHECK_EQ(stackRaw->childOffsetAt(0), (Offset{0, 0}));

  // Re-aligning moves the boundary without changing the constraints it sees, so
  // its recorded contents are still valid at its own origin.
  stackRaw->setAlignment(Alignment::center());
  const Scene after = owner.drawFrame();

  CHECK_EQ(stackRaw->childOffsetAt(0), (Offset{80, 40}));
  CHECK_EQ(boundary->boundaryListIfAny()->revision(), innerRev);
  CHECK_EQ(innerRaw->paintCount(), innerPaints);
  // Only the parent's DrawList offset changed.
  CHECK_NE(after.root->revision(), rootRev);
  CHECK(dumpDisplayList(*after.root).find("DrawList at=(80,40)") != std::string::npos);
}

TEST(pipeline_a_render_object_observing_a_listenable_repaints_without_layout) {
  BasicTree t;
  t.owner.drawFrame();

  Signal signal;
  t.a->repaintOn(&signal);
  CHECK(signal.hasListeners());

  const std::uint32_t layoutsBefore = t.a->layoutCount();
  signal.fire();
  CHECK_EQ(t.owner.dirtyLayoutCount(), std::size_t{0});
  CHECK_EQ(t.owner.dirtyPaintCount(), std::size_t{1});

  t.owner.drawFrame();
  CHECK_EQ(t.owner.stats().layouts, 0);
  CHECK_EQ(t.owner.stats().boundariesRepainted, 1);
  CHECK_EQ(t.a->layoutCount(), layoutsBefore);

  // The subscription is owned by the render object and unhooks with it.
  t.a->repaintOn(nullptr);
  CHECK(!signal.hasListeners());
}

// ---------------------------------------------------------------------------
// Phase separation
// ---------------------------------------------------------------------------

TEST(pipeline_painting_must_not_dirty_layout) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{100, 100});
  view->setChild(make<RenderIllegalPainter>());
  owner.setRootNode(view.get());
  owner.flushLayout();
  CHECK_THROWS(owner.flushPaint());
}

TEST(pipeline_painting_outside_the_paint_phase_is_a_contract_violation) {
  BasicTree t;
  t.owner.drawFrame();

  DisplayList list;
  list.beginRecording();
  PaintingContext ctx(list);
  // The pipeline is Idle, so nothing attached may be painted.
  CHECK_THROWS(t.view->paintWithContext(ctx, Offset::zero()));
}

TEST(pipeline_flushing_paint_with_layout_still_dirty_is_a_contract_violation) {
  BasicTree t;
  t.owner.drawFrame();
  t.a->setPreferredSize({50, 20});
  CHECK_THROWS(t.owner.flushPaint());
}

TEST(pipeline_reentering_a_phase_is_a_contract_violation) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{100, 100});
  view->setChild(make<RenderReentrantLayout>());
  owner.setRootNode(view.get());

  CHECK_THROWS(owner.flushLayout());
  // The scope restored the phase on the way out, so teardown is not a second
  // confusing failure.
  CHECK_EQ(owner.phase(), PipelinePhase::Idle);
  owner.setRootNode(nullptr);
}

TEST(pipeline_a_node_still_needing_layout_cannot_be_painted) {
  auto probe = make<RenderProbe>(Size{10, 10});
  DisplayList list;
  list.beginRecording();
  PaintingContext ctx(list);
  CHECK_THROWS(probe->paintWithContext(ctx, Offset::zero()));
}

// ---------------------------------------------------------------------------
// Dirty-list hygiene
// ---------------------------------------------------------------------------

TEST(pipeline_removing_a_subtree_purges_its_dirty_entries) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto sized = make<RenderConstrainedBox>(BoxConstraints::tightFor(40, 20));
  auto probe = make<RenderProbe>(Size{40, 20});
  RenderProbe* probeRaw = probe.get();
  RenderFlex* rowRaw = row.get();
  sized->setChild(std::move(probe));
  row->addChild(std::move(sized));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());
  owner.drawFrame();

  // Two independent dirty boundaries: the probe registers itself because it sits
  // under tight constraints, and the row registers itself because the view gave
  // it tight constraints too.
  probeRaw->setPreferredSize({30, 20});
  rowRaw->setSpacing(4);
  CHECK_EQ(owner.dirtyLayoutCount(), std::size_t{2});

  // Removing the subtree destroys the probe while it is still registered. The
  // row is already dirty, so dropping the child adds no new entry -- the count
  // going to one is exactly the purge.
  rowRaw->removeChildAt(0);
  CHECK_EQ(owner.dirtyLayoutCount(), std::size_t{1});

  owner.drawFrame();
  CHECK_EQ(rowRaw->childCount(), std::size_t{0});
  CHECK_EQ(owner.stats().boundariesRelaidOut, 1);
}

TEST(pipeline_a_detached_subtree_is_skipped_rather_than_laid_out) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto sized = make<RenderConstrainedBox>(BoxConstraints::tightFor(40, 20));
  auto probe = make<RenderProbe>(Size{40, 20});
  RenderProbe* probeRaw = probe.get();
  RenderConstrainedBox* sizedRaw = sized.get();
  RenderFlex* rowRaw = row.get();
  sized->setChild(std::move(probe));
  row->addChild(std::move(sized));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());
  owner.drawFrame();

  probeRaw->setPreferredSize({30, 20});
  // Keep the subtree alive but detached.
  std::unique_ptr<RenderBox> orphan = rowRaw->removeChildAt(0);
  CHECK(!orphan->attached());
  CHECK(!probeRaw->attached());

  const std::uint32_t probeLayouts = probeRaw->layoutCount();
  owner.drawFrame();
  CHECK_EQ(probeRaw->layoutCount(), probeLayouts);  // never laid out again
  CHECK(probeRaw->needsLayout());                   // and still dirty, for reuse
  (void)sizedRaw;
}

TEST(pipeline_reattaching_a_dirty_boundary_reregisters_it) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto isolated = make<RenderRepaintBoundary>();
  auto probe = make<RenderProbe>(Size{40, 20});
  RenderProbe* probeRaw = probe.get();
  RenderRepaintBoundary* boundary = isolated.get();
  RenderFlex* rowRaw = row.get();
  isolated->setChild(std::move(probe));
  row->addChild(std::move(isolated));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());
  owner.drawFrame();

  std::unique_ptr<RenderBox> orphan = rowRaw->removeChildAt(0);
  probeRaw->setColor(Color::argb(0xFF777777));  // dirtied while detached
  CHECK_EQ(owner.dirtyPaintCount(), std::size_t{0});

  rowRaw->addChild(std::move(orphan));
  // attach() re-runs the invalidation that had nowhere to register.
  CHECK_EQ(owner.dirtyPaintCount(), std::size_t{1});

  owner.drawFrame();
  CHECK(!owner.needsFrame());
  CHECK(boundary->boundaryListIfAny() != nullptr);
}

TEST(pipeline_destroying_the_root_while_installed_leaves_the_owner_safe) {
  PipelineOwner owner;
  {
    auto view = make<RenderView>(Size{200, 100});
    view->setChild(make<RenderProbe>(Size{40, 20}));
    owner.setRootNode(view.get());
    owner.drawFrame();
    static_cast<RenderView*>(owner.rootNode())->setSurface({300, 150});
    CHECK_EQ(owner.dirtyLayoutCount(), std::size_t{1});
  }  // the root is destroyed here, still installed and still dirty
  CHECK_EQ(owner.rootNode(), nullptr);
  CHECK_EQ(owner.dirtyLayoutCount(), std::size_t{0});
  owner.drawFrame();  // must not touch freed memory
}

// ---------------------------------------------------------------------------
// Layout dirtied during layout
// ---------------------------------------------------------------------------

TEST(pipeline_resizing_the_surface_relayouts_from_the_root) {
  BasicTree t;
  t.owner.drawFrame();

  t.view->setSurface({400, 200});
  t.owner.drawFrame();

  CHECK_EQ(t.view->size(), (Size{400, 200}));
  CHECK_EQ(t.align->size(), (Size{400, 200}));
  CHECK_EQ(t.owner.stats().layoutPasses, 1);
  CHECK_EQ(t.owner.drawFrame().surface, (Size{400, 200}));
}

TEST(pipeline_layout_that_never_converges_is_capped_and_reported) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto leftBox = make<RenderConstrainedBox>(BoxConstraints::tightFor(10, 10));
  auto rightBox = make<RenderConstrainedBox>(BoxConstraints::tightFor(10, 10));
  auto left = make<RenderPingPong>();
  auto right = make<RenderPingPong>();
  RenderPingPong* leftRaw = left.get();
  RenderPingPong* rightRaw = right.get();
  leftBox->setChild(std::move(left));
  rightBox->setChild(std::move(right));
  row->addChild(std::move(leftBox));
  row->addChild(std::move(rightBox));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());

  // Tight constraints make each of them its own relayout boundary, so each
  // re-registers directly and the drain never empties.
  leftRaw->setPartner(rightRaw);
  rightRaw->setPartner(leftRaw);

  CHECK_THROWS(owner.flushLayout());
  // It stopped rather than spinning, and stopped where it said it would.
  CHECK_EQ(owner.stats().layoutPasses, 32);
}

TEST(pipeline_the_convergence_cap_is_per_flush_not_cumulative) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{200, 100});
  auto align = make<RenderPositionedBox>();
  RenderPositionedBox* alignRaw = align.get();
  view->setChild(std::move(align));
  owner.setRootNode(view.get());
  owner.flushLayout();

  // Far more flushes than the cap, without the resetStats() that drawFrame does.
  for (int i = 0; i < 100; ++i) {
    alignRaw->setAlignment(i % 2 == 0 ? Alignment::topLeft() : Alignment::bottomRight());
    owner.flushLayout();
    CHECK_EQ(owner.dirtyLayoutCount(), std::size_t{0});
  }
}

TEST(pipeline_scene_dump_is_stable_and_shows_nested_boundaries_in_place) {
  PipelineOwner owner;
  auto view = make<RenderView>(Size{100, 40});
  auto row = make<RenderFlex>(Axis::Horizontal);
  auto plain = make<RenderProbe>(Size{20, 10}, Color::argb(0xFF112233));
  auto isolated = make<RenderRepaintBoundary>();
  auto inner = make<RenderProbe>(Size{30, 10}, Color::argb(0xFF445566));
  isolated->setChild(std::move(inner));
  row->addChild(std::move(plain));
  row->addChild(std::move(isolated));
  view->setChild(std::move(row));
  owner.setRootNode(view.get());

  const Scene scene = owner.drawFrame();
  // The nested boundary appears inline, at its own origin, with its own
  // revision -- which is what a backend keys its cached translation on.
  CHECK_EQ(dumpScene(scene),
           "Scene surface=100x40 revision=2\n"
           "  DrawRect [0,15 20x10] #FF112233\n"
           "  DrawList at=(20,15) rev=1\n"
           "    DrawRect [0,0 30x10] #FF445566\n");
}
