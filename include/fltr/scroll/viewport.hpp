#pragma once

#include "fltr/core/observable.hpp"
#include "fltr/render/box.hpp"
#include "fltr/scroll/position.hpp"

namespace fltr {

/// A window onto a taller child.
///
/// One child, laid out unbounded along the scroll axis and *painted* at a
/// negative offset inside a clip. Scrolling therefore never runs layout: the
/// offset feeds paint and hit testing only, and the position is observed through
/// the render-attached path, so a frame of scrolling invalidates this object's
/// painting and nothing else.
///
/// It lives here rather than in `render/` for the reason `RenderPointerRegion`
/// does: the render tree keeps its property of depending on no subsystem above
/// it, and stays testable without one.
class RenderViewport final : public RenderProxyBox {
public:
  RenderViewport(ScrollPosition* position, Axis axis);
  ~RenderViewport() override;

  /// Owns its own display list, so a frame of scrolling re-records a clip and
  /// one reference to the content rather than the content itself.
  bool isRepaintBoundary() const override { return true; }
  const char* typeName() const override { return "Viewport"; }
  std::string describe() const override;

  void setPosition(ScrollPosition* position);
  void setAxis(Axis axis);

  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override;
  bool hitTestChildren(HitTestResult& result, Offset position) override;
  void visitChildrenWithOffsets(FunctionRef<void(RenderObject&, Offset)> visitor) const override {
    if (child_) visitor(*child_, paintOffset());
  }

  /// The offset that brings `target` into view, with `alignment` deciding where
  /// it lands: 0 against the leading edge, 1 against the trailing one.
  float offsetToReveal(const RenderObject& target, float alignment) const;

  /// The nearest offset that brings `target` fully into view, which is the
  /// current one when it already is.
  float offsetToRevealMinimally(const RenderObject& target) const;

private:
  friend class ScrollPosition;

  void forgetPosition() noexcept;
  Offset paintOffset() const noexcept;
  float mainOf(Size size) const noexcept {
    return axis_ == Axis::Vertical ? size.height : size.width;
  }

  ScrollPosition* position_;
  Subscription subscription_;
  Axis axis_;
};

/// Where a scrollbar's thumb sits on its track, given what the offset can do.
struct ScrollbarGeometry {
  float start = 0.0f;
  float extent = 0.0f;

  static ScrollbarGeometry resolve(const ScrollMetrics& metrics, float trackExtent,
                                   float minExtent) noexcept;

  /// The scroll offset a thumb dragged `delta` along its track means. The two
  /// travel different distances, which is the whole point of a scrollbar.
  float scrollForThumbDelta(const ScrollMetrics& metrics, float trackExtent,
                            float delta) const noexcept;
};

/// The thumb, and only the thumb: it fills the track it is placed in and paints
/// one rounded rectangle inside it.
///
/// It observes the position for paint, so dragging the content moves the thumb
/// with no rebuild at all -- which is what keeps a scrollbar from turning every
/// scrolled frame into a build.
class RenderScrollbarThumb final : public RenderBox {
public:
  RenderScrollbarThumb(ScrollPosition* position, Axis axis);

  const char* typeName() const override { return "ScrollbarThumb"; }
  std::string describe() const override;

  void setPosition(ScrollPosition* position);
  void setAxis(Axis axis);
  void setMinExtent(float extent);
  void setColor(Color color);
  void setRadius(BorderRadius radius);
  /// Drives the thumb's fade, which is the one thing about it that is not a
  /// function of the offset.
  void setOpacity(ValueListenable<float>* source);

  Rect thumbRect() const noexcept;
  float opacity() const noexcept { return opacity_.value(); }

  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override;

private:
  /// A position that has been destroyed detached every subscription it handed
  /// out, so this is how the thumb knows its pointer is still good.
  const ScrollPosition* livePosition() const noexcept {
    return positionSubscription_.attached() ? position_ : nullptr;
  }

  ScrollPosition* position_;
  Subscription positionSubscription_;
  Subscription opacitySubscription_;
  Animatable<float> opacity_{1.0f};
  Axis axis_;
  float minExtent_ = 24.0f;
  Color color_{255, 255, 255, 120};
  BorderRadius radius_ = BorderRadius::zero();
};

}  // namespace fltr
