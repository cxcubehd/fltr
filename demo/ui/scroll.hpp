#pragma once

#include <algorithm>
#include <memory>

#include "fltr/core/observable.hpp"
#include "fltr/render/box.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

/// Where a scroll position lives.
///
/// Scrolling is explicitly out of the framework's scope, so the position is
/// demo state -- an ordinary `Observable<float>`, indistinguishable to the
/// framework from any other value the game pushes in. The viewport observes it
/// for *paint*, so a scroll is one re-recorded display list and no layout and
/// no rebuild.
class ScrollController {
public:
  fltr::ValueListenable<float>& position() noexcept { return offset_; }
  float offset() const noexcept { return offset_.value(); }

  float viewportExtent() const noexcept { return viewport_; }
  float contentExtent() const noexcept { return content_; }
  float maxScroll() const noexcept { return std::max(0.0f, content_ - viewport_); }
  /// 0 when everything fits, which is what a scrollbar needs to know to hide.
  float fraction() const noexcept {
    return maxScroll() <= 0.0f ? 0.0f : std::clamp(offset() / maxScroll(), 0.0f, 1.0f);
  }

  void jumpTo(float to) { offset_.set(std::clamp(to, 0.0f, maxScroll())); }
  void scrollBy(float delta) { jumpTo(offset() + delta); }

  /// Written by the viewport at the end of its layout.
  ///
  /// Deliberately silent: notifying from inside the layout phase would ask a
  /// render object to repaint while the pipeline is still laying out, and the
  /// two phases are separate on purpose. The viewport clamps whatever it reads
  /// instead, so a list that shrank under a scrolled-down view still paints
  /// correctly on the very frame it shrank.
  void setExtents(float viewport, float content) noexcept {
    viewport_ = viewport;
    content_ = content;
  }

private:
  fltr::Observable<float> offset_{0.0f};
  float viewport_ = 0.0f;
  float content_ = 0.0f;
};

/// A scrolling viewport, written all the way down to its render object.
///
/// What a viewport actually is, in the box protocol:
///
///   - it takes its size from its constraints alone, which makes it
///     `sizedByParent` and therefore a relayout boundary unconditionally: a row
///     changing height can never reach the page around it;
///   - it lays its child out with an unbounded main axis, so the child reports
///     the full content height;
///   - it paints the child at a negative offset inside a clip;
///   - it hit tests with that same offset applied. `RenderBox::hitTest` already
///     rejects anything outside `paintBounds()`, so a row scrolled out of view
///     is unreachable without the viewport doing anything about it -- checked
///     in the tests rather than assumed.
///
/// No slivers, no lazy building, no physics: a few hundred rows built eagerly,
/// which is worth measuring in the overlay rather than optimising blind.
class RenderScrollViewport final : public fltr::RenderProxyBox {
public:
  explicit RenderScrollViewport(ScrollController& controller) { setController(controller); }

  const char* typeName() const override { return "ScrollViewport"; }
  std::string describe() const override;

  bool sizedByParent() const override { return true; }
  /// Scrolling re-records this list and nothing above it.
  bool isRepaintBoundary() const override { return true; }

  void setController(ScrollController& controller);
  ScrollController& controller() const noexcept { return *controller_; }

  void performResize() override;
  void performLayout() override;
  void paint(fltr::PaintingContext& context, fltr::Offset offset) override;

  /// The scrolling surface answers for its whole area, so the wheel router can
  /// find it under the cursor even where no row was hit.
  bool hitTestSelf(fltr::Offset) const override { return true; }
  bool hitTestChildren(fltr::HitTestResult& result, fltr::Offset position) override;

private:
  /// Clamped at the point of use rather than at the point of change, because
  /// the content extent is only known after layout.
  float scroll() const noexcept {
    return std::clamp(controller_->offset(), 0.0f, controller_->maxScroll());
  }

  ScrollController* controller_ = nullptr;
  fltr::Subscription subscription_;
};

class ScrollPanel final : public fltr::Configure<ScrollPanel, fltr::SingleChildRenderObjectWidget> {
public:
  struct Args {
    fltr::Key key;
    ScrollController* controller = nullptr;
    fltr::WidgetRef child;
  };
  using Render = RenderScrollViewport;

  explicit ScrollPanel(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.controller != nullptr, "a ScrollPanel needs a controller");
  }

  const char* name() const noexcept override { return "ScrollPanel"; }
  fltr::WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderScrollViewport> createRenderObject(fltr::BuildContext&) const {
    return std::make_unique<RenderScrollViewport>(*args_.controller);
  }
  void updateRenderObject(fltr::BuildContext&, RenderScrollViewport& render) const {
    render.setController(*args_.controller);
  }

private:
  Args args_;
};

/// A value derived from another value.
///
/// The framework has no combinator layer, deliberately -- so a consumer that
/// wants one writes it, and both consumption paths take the result without
/// knowing it was derived. Here it turns a scroll position into the alignment
/// that places a scrollbar thumb, which lets the thumb move with no rebuild at
/// all.
template <class From, class To>
  requires std::equality_comparable<To>
class Mapped final : public fltr::ValueListenable<To> {
public:
  using Fn = To (*)(const From&, void* context);

  Mapped(fltr::ValueListenable<From>& source, Fn fn, void* context)
      : source_(&source), fn_(fn), context_(context), value_(fn(source.value(), context)) {
    fltr::subscribeMember<Mapped, &Mapped::recompute>(source, subscription_, this);
  }

  const To& value() const noexcept override { return value_; }

  /// For a derived value whose inputs are not all observable: the scrollbar's
  /// thumb also moves when the *content* changes, which is a layout result and
  /// not a notification.
  void recompute() {
    To next = fn_(source_->value(), context_);
    if (next == value_) return;
    value_ = next;
    this->notifyListeners();
  }

private:
  fltr::ValueListenable<From>* source_;
  Fn fn_;
  void* context_;
  To value_;
  fltr::Subscription subscription_;
};

}  // namespace fltrdemo
