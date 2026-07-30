#include "fltr/render/box.hpp"

namespace fltr {

RenderBox::~RenderBox() = default;

Size RenderBox::size() const {
  FLTR_EXPECTS(hasSize_, "size read before this box has been laid out");
  if (owner() != nullptr) {
    const RenderObject* active = owner()->activeLayoutNode();
    // Without this a parent could depend on a child's size while the child
    // stayed a relayout boundary, and never be re-laid-out when it changed.
    if (active != nullptr && active == parent()) {
      FLTR_EXPECTS(canParentUseSize_,
                   "parent read a child's size without passing parentUsesSize = true");
    }
  }
  return size_;
}

void RenderBox::layout(BoxConstraints constraints, bool parentUsesSize) {
  FLTR_EXPECTS(constraints.isNormalized(), "layout received non-normalized constraints");

  canParentUseSize_ = parentUsesSize;

  // A node is a relayout boundary when re-laying it out cannot change anything
  // its parent observed:
  //   - the parent never read its size (!parentUsesSize), or
  //   - its size comes only from constraints (sizedByParent), or
  //   - the constraints force exactly one size (isTight), or
  //   - there is no parent at all.
  relayoutBoundary_ =
      (!parentUsesSize || sizedByParent() || constraints.isTight() || parent() == nullptr) ? 1 : 0;

  if (!needsLayout_ && constraints == constraints_) return;
  constraints_ = constraints;

  PipelineOwner* o = owner();
  RenderObject* previousActive = nullptr;
  if (o) {
    previousActive = o->activeLayoutNode();
    o->setActiveLayoutNode(this);
    ++o->mutableStats().layouts;
  }
  ++layoutCount_;

  if (sizedByParent()) {
    doingThisResize_ = true;
    performResize();
    doingThisResize_ = false;
  }
  doingThisLayout_ = true;
  performLayout();
  doingThisLayout_ = false;

  if (o) o->setActiveLayoutNode(previousActive);

  needsLayout_ = false;
  FLTR_ENSURES(hasSize_, "performLayout did not set a size");
  markNeedsPaint();
}

void RenderBox::layoutWithoutResize() {
  // The pipeline root has no parent, which makes it a boundary by definition
  // even before its first layout has computed the flag.
  FLTR_EXPECTS(relayoutBoundary_ == 1 || parent() == nullptr,
               "layoutWithoutResize on a node that is not a relayout boundary");
  FLTR_EXPECTS(needsLayout_, "layoutWithoutResize on a node that is not dirty");

  PipelineOwner* o = owner();
  RenderObject* previousActive = nullptr;
  if (o) {
    previousActive = o->activeLayoutNode();
    o->setActiveLayoutNode(this);
    ++o->mutableStats().layouts;
  }
  ++layoutCount_;

  // No performResize: a sizedByParent node's size depends only on constraints,
  // and those have not changed.
  doingThisLayout_ = true;
  performLayout();
  doingThisLayout_ = false;

  if (o) o->setActiveLayoutNode(previousActive);

  needsLayout_ = false;
  markNeedsPaint();
}

bool RenderBox::hitTest(HitTestResult& result, Offset position) {
  if (!hasSize_ || !paintBounds().contains(position)) return false;
  if (hitTestChildren(result, position) || hitTestSelf(position)) {
    result.add(this, position);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// RenderProxyBox
// ---------------------------------------------------------------------------

RenderProxyBox::~RenderProxyBox() {
  if (child_) dropChild(child_.get());
}

void RenderProxyBox::setChild(std::unique_ptr<RenderBox> child) {
  if (child_) dropChild(child_.get());
  child_ = std::move(child);
  if (child_) adoptChild(child_.get());
  markNeedsLayout();
}

std::unique_ptr<RenderBox> RenderProxyBox::takeChild() {
  if (child_) dropChild(child_.get());
  std::unique_ptr<RenderBox> out = std::move(child_);
  markNeedsLayout();
  return out;
}

void RenderProxyBox::performLayout() {
  if (child_) {
    setSize(layoutChildForSize(*child_, constraints_));
  } else {
    setSize(constraints_.smallest());
  }
}

void RenderProxyBox::paint(PaintingContext& context, Offset offset) {
  if (child_) context.paintChild(*child_, offset);
}

bool RenderProxyBox::hitTestChildren(HitTestResult& result, Offset position) {
  if (!child_) return false;
  return child_->hitTest(result, position);
}

// ---------------------------------------------------------------------------
// RenderShiftedBox
// ---------------------------------------------------------------------------

void RenderShiftedBox::paint(PaintingContext& context, Offset offset) {
  if (child_) context.paintChild(*child_, offset + childOffset_);
}

bool RenderShiftedBox::hitTestChildren(HitTestResult& result, Offset position) {
  if (!child_) return false;
  return result.addWithPaintOffset(
      childOffset_, position,
      [this](HitTestResult& r, Offset p) { return child_->hitTest(r, p); });
}

}  // namespace fltr
