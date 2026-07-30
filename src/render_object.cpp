#include "fltr/render/object.hpp"

#include <algorithm>

namespace fltr {

RenderObject::~RenderObject() = default;

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
  // Stale entries are left in the owner's dirty lists; both flush phases skip
  // any node whose owner is no longer this pipeline. That is cheaper than an
  // O(n) removal on every detach, and detach is the rare operation.
  owner_ = nullptr;
  visitChildren([](RenderObject& child) { child.detach(); });
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
    if (owner_) {
      owner_->nodesNeedingLayout_.push_back(this);
      owner_->requestVisualUpdate();
    }
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
    if (owner_) {
      owner_->nodesNeedingPaint_.push_back(this);
      owner_->requestVisualUpdate();
    }
  } else if (parent_) {
    parent_->markNeedsPaint();
  } else if (owner_) {
    owner_->rootNeedsPaint_ = true;
    owner_->requestVisualUpdate();
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
      // Recorded relative to the boundary's own origin, so moving the boundary
      // changes only the parent's DrawList offset and never re-records it.
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
    // The root is dirty by construction but was never registered, because
    // markNeedsLayout short-circuits on an already-dirty node.
    root_->needsLayout_ = false;
    root_->markNeedsLayout();
    root_->needsPaint_ = false;
    root_->wasRepaintBoundary_ = true;
    root_->markNeedsPaint();
  }
}

void PipelineOwner::flushLayout() {
  FLTR_EXPECTS(phase_ == PipelinePhase::Idle, "flushLayout entered while another phase is active");
  phase_ = PipelinePhase::Layout;

  // Nodes dirtied *during* layout are picked up by the outer loop.
  while (!nodesNeedingLayout_.empty()) {
    std::vector<RenderObject*> dirty;
    dirty.swap(nodesNeedingLayout_);
    // Shallowest first: a parent's layout subsumes any dirty descendants, which
    // are then skipped by the needsLayout guard below.
    std::sort(dirty.begin(), dirty.end(),
              [](const RenderObject* a, const RenderObject* b) { return a->depth_ < b->depth_; });
    for (RenderObject* node : dirty) {
      if (node->needsLayout_ && node->owner_ == this) {
        ++stats_.boundariesRelaidOut;
        node->layoutWithoutResize();
      }
    }
  }

  phase_ = PipelinePhase::Idle;
}

void PipelineOwner::flushPaint() {
  FLTR_EXPECTS(phase_ == PipelinePhase::Idle, "flushPaint entered while another phase is active");
  FLTR_EXPECTS(nodesNeedingLayout_.empty(), "flushPaint with layout still dirty");
  phase_ = PipelinePhase::Paint;

  std::vector<RenderObject*> dirty;
  dirty.swap(nodesNeedingPaint_);
  // Deepest first, so a boundary that repaints on its own is already clean by
  // the time an ancestor boundary embeds it by reference.
  std::sort(dirty.begin(), dirty.end(),
            [](const RenderObject* a, const RenderObject* b) { return b->depth_ < a->depth_; });

  for (RenderObject* node : dirty) {
    if (!node->needsPaint_ || node->owner_ != this) continue;
    DisplayList& sub = node->boundaryList();
    sub.beginRecording();
    PaintingContext ctx(sub);
    node->paintWithContext(ctx, Offset::zero());
    sub.endRecording();
    ++stats_.boundariesRepainted;
    ++recordCounter_;
  }

  rootNeedsPaint_ = false;
  phase_ = PipelinePhase::Idle;
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
