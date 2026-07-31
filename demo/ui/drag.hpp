#pragma once

#include <memory>

#include "fltr/core/callback.hpp"
#include "fltr/render/box.hpp"
#include "fltr/render/hit_test.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

/// What a drag reports.
///
/// `localPosition` is in the target's own box, which is what a control needs to
/// turn a pointer into a value, and `regionSize` travels with it because a
/// widget has no way to ask its render object how big it ended up.
struct DragDetails {
  fltr::Offset localPosition;
  fltr::Offset delta;
  fltr::Size regionSize;
};

class PointerRouter;

/// A box that can be dragged. Nothing more: hover and tap stay with the
/// framework's `Pointer`, and this exists only because the gesture layer has
/// tap and hover and nothing else.
class RenderDragTarget final : public fltr::RenderProxyBox {
public:
  struct Callbacks {
    fltr::Callback<void(const DragDetails&)> onDragStart;
    fltr::Callback<void(const DragDetails&)> onDragUpdate;
    fltr::Callback<void()> onDragEnd;
  };

  RenderDragTarget(PointerRouter& router, const Callbacks& callbacks)
      : router_(&router), callbacks_(callbacks) {}
  ~RenderDragTarget() override;

  const char* typeName() const override { return "DragTarget"; }

  void setCallbacks(const Callbacks& callbacks) { callbacks_ = callbacks; }
  const Callbacks& callbacks() const noexcept { return callbacks_; }

  /// Opaque within its bounds: the router asks the render tree what is under
  /// the press, and a control that answers "my child was not hit" would not be
  /// draggable in its own margins.
  bool hitTestSelf(fltr::Offset) const override { return true; }

private:
  PointerRouter* router_;
  Callbacks callbacks_;
};

class DragTarget final : public fltr::Configure<DragTarget, fltr::SingleChildRenderObjectWidget> {
public:
  struct Args {
    fltr::Key key;
    PointerRouter* router = nullptr;
    fltr::Callback<void(const DragDetails&)> onDragStart;
    fltr::Callback<void(const DragDetails&)> onDragUpdate;
    fltr::Callback<void()> onDragEnd;
    fltr::WidgetRef child;
  };
  using Render = RenderDragTarget;

  explicit DragTarget(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.router != nullptr, "a DragTarget needs a router");
  }

  const char* name() const noexcept override { return "DragTarget"; }
  fltr::WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderDragTarget> createRenderObject(fltr::BuildContext&) const {
    return std::make_unique<RenderDragTarget>(*args_.router, callbacks());
  }
  void updateRenderObject(fltr::BuildContext&, RenderDragTarget& render) const {
    render.setCallbacks(callbacks());
  }

private:
  RenderDragTarget::Callbacks callbacks() const noexcept {
    return {args_.onDragStart, args_.onDragUpdate, args_.onDragEnd};
  }

  Args args_;
};

/// The two things raylib reports that the framework has no route for: dragging,
/// and the wheel.
///
/// DECISION -- this is demo-local, and it is worth being precise about what
/// that costs. `PointerBinding::dispatch` offers a pointer only to boxes whose
/// `asPointerRegion()` is non-null, so a recognizer written outside the library
/// cannot join the gesture arena at all. The router therefore runs its own hit
/// test against the render tree on mouse-down and drives the topmost
/// `RenderDragTarget` it finds. The costs, all of them:
///
///   - a drag does not contest with a tap. A draggable control nested inside a
///     region that wants the tap would fire both, so the demo never nests one
///     that way -- an invariant a real drag recognizer in the arena would
///     enforce instead of the author remembering it.
///   - there is no touch slop: the press starts the drag. For a slider that is
///     the wanted behaviour (clicking the track jumps to the value), and for a
///     scrollbar thumb it is harmless.
///   - one extra hit test per press, counted separately in the debug overlay so
///     it is visible rather than hidden.
///   - the drag extrapolates the local position from the global delta, which is
///     exact under translation and wrong under scale. Nothing draggable in the
///     demo sits under a scaling transform.
class PointerRouter {
public:
  /// Runs after the framework has been told about the press, so a tap that
  /// wins the arena has already been recognised.
  void down(fltr::WidgetBinding& binding, fltr::Offset position);
  void move(fltr::Offset position);
  void up();
  void cancel();

  /// `ticks` is raylib's wheel movement; the router turns it into a scroll of
  /// whatever viewport is under the cursor.
  void wheel(fltr::WidgetBinding& binding, fltr::Offset position, float ticks, float lineHeight);

  bool dragging() const noexcept { return target_ != nullptr; }

  /// The demo's own hit tests, kept apart from `PointerBinding::hitTestCount()`
  /// so the overlay never flatters the framework with the demo's work.
  int hitTests() const noexcept { return hitTests_; }

private:
  friend class RenderDragTarget;

  void forget(RenderDragTarget& target) noexcept;
  const fltr::HitTestResult& hitTest(fltr::WidgetBinding& binding, fltr::Offset position);

  fltr::HitTestResult path_;
  RenderDragTarget* target_ = nullptr;
  fltr::Offset localAtDown_;
  fltr::Offset globalAtDown_;
  fltr::Offset lastGlobal_;
  int hitTests_ = 0;
};

}  // namespace fltrdemo
