#include "ui/scroll.hpp"

#include "fltr/debug/format.hpp"

namespace fltrdemo {

void RenderScrollViewport::setController(ScrollController& controller) {
  if (controller_ == &controller) return;
  controller_ = &controller;
  // Paint, not layout: where the content sits inside the viewport changes what
  // is drawn and what is under the cursor, and neither of those is a size.
  observeForPaint(subscription_, &controller.position());
  markNeedsPaint();
}

std::string RenderScrollViewport::describe() const {
  return "scroll=" + fltr::dbg::str(scroll()) + " of " + fltr::dbg::str(controller_->maxScroll());
}

void RenderScrollViewport::performResize() {
  FLTR_EXPECTS(constraints_.hasBoundedHeight() && constraints_.hasBoundedWidth(),
               "a scroll viewport needs bounded constraints: it is the thing that decides how "
               "much of an unbounded child is visible");
  setSize(constraints_.biggest());
}

void RenderScrollViewport::performLayout() {
  if (!child_) {
    controller_->setExtents(size_.height, 0.0f);
    return;
  }
  // Tight across, unbounded along: the child reports how tall the content
  // really is, which is the one thing the viewport cannot know on its own.
  const fltr::Size content = layoutChildForSize(
      *child_, fltr::BoxConstraints{size_.width, size_.width, 0.0f, fltr::kInf});
  controller_->setExtents(size_.height, content.height);
}

void RenderScrollViewport::paint(fltr::PaintingContext& context, fltr::Offset offset) {
  if (!child_) return;
  const fltr::Offset shifted = offset + fltr::Offset{0.0f, -scroll()};
  context.pushClipRect(fltr::Rect::fromOriginSize(offset, size_), fltr::BorderRadius::zero(),
                       [this, shifted](fltr::PaintingContext& c) { c.paintChild(*child_, shifted); });
}

bool RenderScrollViewport::hitTestChildren(fltr::HitTestResult& result, fltr::Offset position) {
  if (!child_) return false;
  return result.addWithPaintOffset(
      fltr::Offset{0.0f, -scroll()}, position,
      [this](fltr::HitTestResult& r, fltr::Offset p) { return child_->hitTest(r, p); });
}

}  // namespace fltrdemo
