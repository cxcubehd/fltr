#include "fltr/render/object.hpp"

#include <algorithm>
#include <vector>

namespace fltr {

RenderObject::~RenderObject() {
  // Normal teardown drops a child before destroying it, so this only fires for a
  // root destroyed while still installed. The flush guards cannot cover it: they
  // must read a node to decide whether to skip it.
  if (owner_) {
    PipelineOwner* o = owner_;
    owner_ = nullptr;
    if (o->root_ == this) o->root_ = nullptr;
    o->purgeDetachedDirtyNodes();
  }
}

// ---------------------------------------------------------------------------
// Coordinate spaces
// ---------------------------------------------------------------------------

void RenderObject::applyPaintTransform(const RenderObject& child, Transform2D& transform) const {
  Offset offset;
  visitChildrenWithOffsets([&](RenderObject& candidate, Offset o) {
    if (&candidate == &child) offset = o;
  });
  transform = Transform2D::translation(offset).then(transform);
}

Transform2D RenderObject::getTransformTo(const RenderObject* ancestor) const {
  if (this == ancestor) return Transform2D::identity();
  if (parent_ == nullptr) {
    FLTR_EXPECTS(ancestor == nullptr, "getTransformTo was given a node that is not an ancestor");
    return Transform2D::identity();
  }
  Transform2D transform = parent_->getTransformTo(ancestor);
  parent_->applyPaintTransform(*this, transform);
  return transform;
}

Offset RenderObject::globalToLocal(Offset global, const RenderObject* ancestor) const {
  Transform2D inverse;
  if (!getTransformTo(ancestor).invert(inverse)) return Offset::zero();
  return inverse.apply(global);
}

// ---------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------

void RenderObject::attach(PipelineOwner* owner) {
  FLTR_EXPECTS(owner != nullptr, "attach requires an owner");
  owner_ = owner;
  // Invalidation that happened while detached was never registered with an
  // owner; re-run it now so the node is reachable from a dirty list.
  if (needsLayout_ && relayoutBoundary_ != -1) {
    needsLayout_ = false;
    markNeedsLayout();
  }
  if (needsPaint_ && wasRepaintBoundary_) {
    needsPaint_ = false;
    markNeedsPaint();
  }
  visitChildren([owner](RenderObject& child) { child.attach(owner); });
}

void RenderObject::detach() {
  PipelineOwner* o = owner_;
  detachSubtree();
  // One sweep for the whole subtree rather than one per node.
  if (o) o->purgeDetachedDirtyNodes();
}

void RenderObject::detachSubtree() {
  owner_ = nullptr;
  visitChildren([](RenderObject& child) { child.detachSubtree(); });
}

void RenderObject::redepthChild(RenderObject* child) {
  if (child->depth_ <= depth_) {
    child->depth_ = depth_ + 1;
    child->visitChildren([child](RenderObject& grandchild) { child->redepthChild(&grandchild); });
  }
}

void RenderObject::resetRelayoutBoundarySubtree() {
  if (relayoutBoundary_ == -1) return;
  relayoutBoundary_ = -1;
  visitChildren([](RenderObject& child) { child.resetRelayoutBoundarySubtree(); });
}

void RenderObject::adoptChild(RenderObject* child) {
  FLTR_EXPECTS(child != nullptr, "adoptChild requires a child");
  FLTR_EXPECTS(child->parent_ == nullptr, "child already has a parent");
  child->parent_ = this;
  redepthChild(child);
  // The new parent may impose different constraints, so any cached boundary
  // decision in the subtree is stale.
  child->resetRelayoutBoundarySubtree();
  if (owner_) child->attach(owner_);
  markNeedsLayout();
}

void RenderObject::dropChild(RenderObject* child) {
  FLTR_EXPECTS(child != nullptr, "dropChild requires a child");
  FLTR_EXPECTS(child->parent_ == this, "dropChild called for a node that is not a child");
  child->resetRelayoutBoundarySubtree();
  child->parent_ = nullptr;
  if (child->owner_) child->detach();
  markNeedsLayout();
}

// ---------------------------------------------------------------------------
// Invalidation
// ---------------------------------------------------------------------------

void RenderObject::markNeedsLayout() {
  if (needsLayout_) return;
  FLTR_EXPECTS(!owner_ || owner_->phase() != PipelinePhase::Paint,
               "painting must not dirty layout");
  needsLayout_ = true;
  if (relayoutBoundary_ == 1 || parent_ == nullptr) {
    // The walk stops here. Everything above this node keeps its layout.
    if (owner_) owner_->nodesNeedingLayout_.push_back(this);
  } else {
    markParentNeedsLayout();
  }
}

void RenderObject::markParentNeedsLayout() {
  FLTR_EXPECTS(parent_ != nullptr, "markParentNeedsLayout without a parent");
  needsLayout_ = true;
  parent_->markNeedsLayout();
}

void RenderObject::markNeedsLayoutForSizedByParentChange() {
  markNeedsLayout();
  if (parent_) markParentNeedsLayout();
}

void RenderObject::markNeedsPaint() {
  if (needsPaint_) return;
  needsPaint_ = true;
  if (isRepaintBoundary() && wasRepaintBoundary_) {
    // Stops here: nothing above this node is re-recorded.
    if (owner_) owner_->nodesNeedingPaint_.push_back(this);
  } else if (parent_) {
    parent_->markNeedsPaint();
  } else if (owner_) {
    // A root that has not painted yet, so `wasRepaintBoundary_` is still false.
    // It is the outermost boundary by definition and registers as one.
    FLTR_ASSERT(isRepaintBoundary(), "the pipeline root must be a repaint boundary");
    owner_->nodesNeedingPaint_.push_back(this);
  }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

DisplayList& RenderObject::boundaryList() {
  FLTR_EXPECTS(isRepaintBoundary(), "only a repaint boundary owns a display list");
  if (!boundaryList_) boundaryList_ = std::make_unique<DisplayList>();
  return *boundaryList_;
}

void RenderObject::paintWithContext(PaintingContext& context, Offset offset) {
  FLTR_EXPECTS(!needsLayout_, "cannot paint a node that still needs layout");
  // The other half of phase separation: markNeedsLayout refuses to run during
  // paint, and this refuses to paint outside it. A detached tree has no owner
  // and no phase, which is how the render-tree tests paint by hand.
  FLTR_EXPECTS(owner_ == nullptr || owner_->phase() == PipelinePhase::Paint,
               "painting outside the paint phase");
  needsPaint_ = false;
  wasRepaintBoundary_ = isRepaintBoundary();
  ++paintCount_;
  if (owner_) ++owner_->stats_.paints;
  paint(context, offset);
  FLTR_ENSURES(!needsLayout_, "paint() must not dirty layout");
}

void PaintingContext::paintChild(RenderObject& child, Offset offset) {
  if (child.isRepaintBoundary()) {
    DisplayList& sub = child.boundaryList();
    if (child.needsPaint() || sub.revision() == 0) {
      sub.beginRecording();
      PaintingContext inner(sub);
      // At the boundary's own origin, so moving it re-records nothing.
      child.paintWithContext(inner, Offset::zero());
      sub.endRecording();
      if (child.owner()) {
        ++child.owner()->stats_.boundariesRepainted;
        ++child.owner()->recordCounter_;
      }
    }
    list_.drawList(&sub, offset);
  } else {
    child.paintWithContext(*this, offset);
  }
}

// ---------------------------------------------------------------------------
// PipelineOwner
// ---------------------------------------------------------------------------

PipelineOwner::~PipelineOwner() {
  if (root_) root_->detach();
}

void PipelineOwner::purgeDetachedDirtyNodes() {
  const auto gone = [this](const RenderObject* n) { return n->owner_ != this; };
  std::erase_if(nodesNeedingLayout_, gone);
  std::erase_if(nodesNeedingPaint_, gone);
  // A node detached from inside a flush can be sitting in the scratch buffer
  // that flush is walking. Blank those rather than erasing them, so the
  // iterators stay valid.
  const auto blank = [&gone](std::vector<RenderObject*>& scratch) {
    for (RenderObject*& n : scratch) {
      if (n != nullptr && gone(n)) n = nullptr;
    }
  };
  blank(layoutScratch_);
  blank(paintScratch_);
}

void PipelineOwner::setRootNode(RenderObject* root) {
  if (root_ == root) return;
  if (root_) root_->detach();
  root_ = root;
  nodesNeedingLayout_.clear();
  nodesNeedingPaint_.clear();
  if (root_) {
    FLTR_EXPECTS(root_->isRepaintBoundary(),
                 "the pipeline root must be a repaint boundary so it owns a display list");
    root_->attach(this);
    // A node with no parent is a relayout boundary by definition.
    root_->relayoutBoundary_ = 1;
    // Dirty by construction but never registered, because both mark* calls
    // short-circuit on an already-dirty node.
    root_->needsLayout_ = false;
    root_->markNeedsLayout();
    root_->needsPaint_ = false;
    root_->markNeedsPaint();
  }
}

void PipelineOwner::flushLayout() {
  FLTR_EXPECTS(phase_ == PipelinePhase::Idle, "flushLayout entered while another phase is active");
  PhaseScope scope(*this, PipelinePhase::Layout);

  // Laying a node out can dirty a boundary not yet visited, so the list is
  // drained rather than swept once. The cap applies in every build, so a pair of
  // nodes dirtying each other degrades to a stale frame rather than a freeze.
  static constexpr int kMaxLayoutPasses = 32;
  int passes = 0;
  while (!nodesNeedingLayout_.empty() && passes < kMaxLayoutPasses) {
    ++passes;
    ++stats_.layoutPasses;
    layoutScratch_.clear();
    layoutScratch_.swap(nodesNeedingLayout_);
    // Shallowest first, so a parent's layout subsumes dirty descendants and the
    // needsLayout guard below then skips them.
    std::sort(layoutScratch_.begin(), layoutScratch_.end(),
              [](const RenderObject* a, const RenderObject* b) { return a->depth_ < b->depth_; });
    for (RenderObject* node : layoutScratch_) {
      if (node != nullptr && node->needsLayout_ && node->owner_ == this) {
        ++stats_.boundariesRelaidOut;
        node->layoutWithoutResize();
      }
    }
  }
  FLTR_ENSURES(nodesNeedingLayout_.empty(),
               "layout did not converge: a render object keeps dirtying itself during layout");
}

void PipelineOwner::flushPaint() {
  FLTR_EXPECTS(phase_ == PipelinePhase::Idle, "flushPaint entered while another phase is active");
  FLTR_EXPECTS(nodesNeedingLayout_.empty(), "flushPaint with layout still dirty");
  {
    PhaseScope scope(*this, PipelinePhase::Paint);

    paintScratch_.clear();
    paintScratch_.swap(nodesNeedingPaint_);
    // Deepest first, so a boundary that repaints on its own is already clean by
    // the time an ancestor boundary embeds it by reference.
    std::sort(paintScratch_.begin(), paintScratch_.end(),
              [](const RenderObject* a, const RenderObject* b) { return b->depth_ < a->depth_; });

    for (RenderObject* node : paintScratch_) {
      if (node == nullptr || !node->needsPaint_ || node->owner_ != this) continue;
      DisplayList& sub = node->boundaryList();
      sub.beginRecording();
      PaintingContext ctx(sub);
      node->paintWithContext(ctx, Offset::zero());
      sub.endRecording();
      ++stats_.boundariesRepainted;
      ++recordCounter_;
    }
  }
  // Checked outside the phase, so reporting it does not itself run under Paint.
  FLTR_ENSURES(nodesNeedingPaint_.empty(), "painting dirtied painting");
}

Scene PipelineOwner::drawFrame() {
  resetStats();
  flushLayout();
  flushPaint();
  return scene();
}

Scene PipelineOwner::scene() const {
  Scene s;
  if (root_) {
    s.root = root_->boundaryListIfAny();
    s.surface = root_->paintBounds().size();
  }
  s.revision = recordCounter_;
  return s;
}

}  // namespace fltr
