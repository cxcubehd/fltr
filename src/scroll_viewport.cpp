#include "fltr/scroll/viewport.hpp"

#include "fltr/debug/format.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// RenderViewport
// ---------------------------------------------------------------------------

RenderViewport::RenderViewport(ScrollPosition* position, Axis axis)
    : position_(nullptr), axis_(axis) {
  setPosition(position);
}

RenderViewport::~RenderViewport() {
  if (position_ && position_->viewport_ == this) position_->viewport_ = nullptr;
}

void RenderViewport::forgetPosition() noexcept {
  position_ = nullptr;
  subscription_.detach();
}

std::string RenderViewport::describe() const {
  return std::string(axis_ == Axis::Vertical ? "axis=y" : "axis=x") +
         " offset=" + dbg::str(position_ ? position_->pixels() : 0.0f);
}

void RenderViewport::setPosition(ScrollPosition* position) {
  if (position == position_) return;
  if (position_ && position_->viewport_ == this) position_->viewport_ = nullptr;
  position_ = position;
  if (position_) position_->viewport_ = this;
  observeForPaint(subscription_, position_);
  markNeedsLayout();
}

void RenderViewport::setAxis(Axis axis) {
  if (axis == axis_) return;
  axis_ = axis;
  markNeedsLayout();
}

Offset RenderViewport::paintOffset() const noexcept {
  if (!position_) return Offset::zero();
  const float pixels = position_->pixels();
  return axis_ == Axis::Vertical ? Offset{0.0f, -pixels} : Offset{-pixels, 0.0f};
}

void RenderViewport::performLayout() {
  if (!child_) {
    setSize(constraints_.smallest());
    if (position_) {
      position_->applyViewportDimension(mainOf(size_));
      position_->applyContentDimensions(0.0f, 0.0f);
    }
    return;
  }

  // The scroll axis is released and the cross axis is passed through, which is
  // what makes the child report how tall it actually wants to be.
  const BoxConstraints inner = axis_ == Axis::Vertical
                                   ? constraints_.copyWith({}, {}, 0.0f, kInf)
                                   : constraints_.copyWith(0.0f, kInf, {}, {});
  const Size childSize = layoutChildForSize(*child_, inner);
  setSize(constraints_.constrain(childSize));

  if (!position_) return;
  const float viewport = mainOf(size_);
  position_->applyViewportDimension(viewport);
  position_->applyContentDimensions(0.0f, std::max(0.0f, mainOf(childSize) - viewport));
}

void RenderViewport::paint(PaintingContext& context, Offset offset) {
  if (!child_) return;
  const Offset scrolled = offset + paintOffset();
  context.pushClipRect(Rect::fromOriginSize(offset, size_), BorderRadius::zero(),
                       [this, scrolled](PaintingContext& c) { c.paintChild(*child_, scrolled); });
}

bool RenderViewport::hitTestChildren(HitTestResult& result, Offset position) {
  if (!child_) return false;
  // Nothing outside the viewport is reachable, which RenderBox::hitTest has
  // already established by testing this box's own bounds first.
  return result.addWithPaintOffset(
      paintOffset(), position,
      [this](HitTestResult& r, Offset p) { return child_->hitTest(r, p); });
}

float RenderViewport::offsetToReveal(const RenderObject& target, float alignment) const {
  if (!position_) return 0.0f;
  const Rect rect = target.localToGlobalRect(target.paintBounds(), this);
  const bool vertical = axis_ == Axis::Vertical;
  const float leading = vertical ? rect.top : rect.left;
  const float extent = vertical ? rect.height() : rect.width();
  // `leading` is where the target sits in the viewport right now, so adding the
  // current offset converts it back to a position in the content.
  return position_->pixels() + leading - alignment * (mainOf(size_) - extent);
}

// ---------------------------------------------------------------------------
// ScrollbarGeometry
// ---------------------------------------------------------------------------

ScrollbarGeometry ScrollbarGeometry::resolve(const ScrollMetrics& metrics, float trackExtent,
                                             float minExtent) noexcept {
  const float content = metrics.viewportDimension + metrics.maxScrollExtent -
                        metrics.minScrollExtent;
  if (content <= 0.0f || trackExtent <= 0.0f) return {};
  const float extent =
      std::clamp(trackExtent * metrics.viewportDimension / content, std::min(minExtent, trackExtent),
                 trackExtent);
  const float range = metrics.maxScrollExtent - metrics.minScrollExtent;
  const float travelled =
      range > 0.0f ? std::clamp((metrics.pixels - metrics.minScrollExtent) / range, 0.0f, 1.0f)
                   : 0.0f;
  return {(trackExtent - extent) * travelled, extent};
}

float ScrollbarGeometry::scrollForThumbDelta(const ScrollMetrics& metrics, float trackExtent,
                                             float delta) const noexcept {
  const float travel = trackExtent - extent;
  if (travel <= 0.0f) return 0.0f;
  return delta * (metrics.maxScrollExtent - metrics.minScrollExtent) / travel;
}

// ---------------------------------------------------------------------------
// RenderScrollbarThumb
// ---------------------------------------------------------------------------

RenderScrollbarThumb::RenderScrollbarThumb(ScrollPosition* position, Axis axis)
    : position_(nullptr), axis_(axis) {
  setPosition(position);
}

std::string RenderScrollbarThumb::describe() const {
  const Rect rect = thumbRect();
  return "thumb=" + dbg::str(axis_ == Axis::Vertical ? rect.top : rect.left) + ".." +
         dbg::str(axis_ == Axis::Vertical ? rect.bottom : rect.right);
}

void RenderScrollbarThumb::setPosition(ScrollPosition* position) {
  if (position == position_) return;
  position_ = position;
  observeForPaint(positionSubscription_, position_);
  markNeedsPaint();
}

void RenderScrollbarThumb::setAxis(Axis axis) {
  if (axis == axis_) return;
  axis_ = axis;
  markNeedsPaint();
}

void RenderScrollbarThumb::setMinExtent(float extent) {
  if (extent == minExtent_) return;
  minExtent_ = extent;
  markNeedsPaint();
}

void RenderScrollbarThumb::setColor(Color color) {
  if (color == color_) return;
  color_ = color;
  markNeedsPaint();
}

void RenderScrollbarThumb::setRadius(BorderRadius radius) {
  if (radius == radius_) return;
  radius_ = radius;
  markNeedsPaint();
}

void RenderScrollbarThumb::setOpacity(ValueListenable<float>* source) {
  if (!opacity_.setSource(source)) return;
  observeForPaint(opacitySubscription_, source);
  markNeedsPaint();
}

void RenderScrollbarThumb::performLayout() {
  FLTR_EXPECTS(constraints_.hasBoundedWidth() && constraints_.hasBoundedHeight(),
               "a scrollbar thumb needs a bounded track to fill");
  setSize(constraints_.biggest());
}

Rect RenderScrollbarThumb::thumbRect() const noexcept {
  const ScrollPosition* position = livePosition();
  if (!position) return Rect::zero();
  const bool vertical = axis_ == Axis::Vertical;
  const ScrollbarGeometry geometry = ScrollbarGeometry::resolve(
      position->metrics(), vertical ? size_.height : size_.width, minExtent_);
  if (geometry.extent <= 0.0f) return Rect::zero();
  return vertical ? Rect::fromLTWH(0.0f, geometry.start, size_.width, geometry.extent)
                  : Rect::fromLTWH(geometry.start, 0.0f, geometry.extent, size_.height);
}

void RenderScrollbarThumb::paint(PaintingContext& context, Offset offset) {
  const Rect rect = thumbRect();
  if (rect.isEmpty()) return;
  context.list().drawRRect(rect.shift(offset), radius_, color_.scaleAlpha(opacity()));
}

}  // namespace fltr
