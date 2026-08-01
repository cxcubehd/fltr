#pragma once

#include <memory>
#include <string>

#include "fltr/core/callback.hpp"
#include "fltr/gestures/binding.hpp"
#include "fltr/gestures/drag.hpp"
#include "fltr/gestures/events.hpp"
#include "fltr/gestures/long_press.hpp"
#include "fltr/gestures/multitap.hpp"
#include "fltr/gestures/tap.hpp"
#include "fltr/render/box.hpp"

namespace fltr {

/// What a Pointer widget configures, in one piece so the render object has a
/// single place to notice a change.
struct PointerCallbacks {
  Callback<void()> onEnter;
  Callback<void()> onExit;

  Callback<void()> onTapDown;
  Callback<void()> onTap;
  Callback<void()> onTapCancel;
  Callback<void()> onDoubleTap;

  Callback<void(const LongPressStartDetails&)> onLongPress;
  Callback<void(const LongPressMoveUpdateDetails&)> onLongPressMoveUpdate;
  Callback<void(const LongPressEndDetails&)> onLongPressEnd;
  Callback<void()> onLongPressCancel;

  DragAxis dragAxis = DragAxis::Pan;
  DragStartBehavior dragStartBehavior = DragStartBehavior::Start;
  Callback<void(const DragDownDetails&)> onDragDown;
  Callback<void(const DragStartDetails&)> onDragStart;
  Callback<void(const DragUpdateDetails&)> onDragUpdate;
  Callback<void(const DragEndDetails&)> onDragEnd;
  Callback<void()> onDragCancel;

  /// A wheel notch or trackpad pan over this region. Returning true consumes it,
  /// so nothing further out sees it.
  Callback<bool(const PointerSignalEvent&)> onSignal;

  /// Which buttons every gesture on this region answers to.
  PointerButtons buttons{PointerButton::Primary};
};

/// The one render object that consumes pointer events.
///
/// It lives with the gestures rather than in `render/` so the render tree keeps
/// the property it was built with in M2: no dependency on the gesture layer at
/// all, and testable without one.
///
/// Recognizers are created only once something wants them, so a region that only
/// reports hover or asks for a cursor holds none at all.
class RenderPointerRegion final : public RenderProxyBox {
public:
  RenderPointerRegion(PointerBinding& binding, HitTestBehavior behavior);
  ~RenderPointerRegion() override;

  const char* typeName() const override { return "PointerRegion"; }
  std::string describe() const override;

  void setBehavior(HitTestBehavior behavior);
  void setCursor(MouseCursor cursor);
  void setCallbacks(const PointerCallbacks& callbacks);

  RenderPointerRegion* asPointerRegion() noexcept override { return this; }

  bool hitTest(HitTestResult& result, Offset position) override;

  bool wantsHover() const noexcept { return onEnter_ || onExit_; }
  bool tracksMouse() const noexcept { return wantsHover() || cursor_ != MouseCursor::Defer; }
  bool hovered() const noexcept { return hovered_; }
  MouseCursor cursor() const noexcept { return cursor_; }

  /// How many recognizers this region actually holds. The claim that an inert
  /// region costs nothing is a count, not a comment.
  std::size_t recognizerCount() const noexcept;

private:
  friend class MouseTracker;
  friend class PointerBinding;

  /// Creates `slot` when `wanted`, destroys it when not, and hands a fresh one
  /// the region's coordinate space and button filter.
  template <class R>
  R* syncRecognizer(std::unique_ptr<R>& slot, bool wanted, PointerButtons buttons);

  void receiveDown(const PointerEvent& down);
  bool receiveSignal(const PointerSignalEvent& signal);
  void enter();
  void exit();

  PointerBinding* binding_;
  std::unique_ptr<TapGestureRecognizer> tap_;
  std::unique_ptr<DoubleTapGestureRecognizer> doubleTap_;
  std::unique_ptr<LongPressGestureRecognizer> longPress_;
  std::unique_ptr<DragGestureRecognizer> drag_;
  Callback<void()> onEnter_;
  Callback<void()> onExit_;
  Callback<bool(const PointerSignalEvent&)> onSignal_;
  HitTestBehavior behavior_;
  MouseCursor cursor_ = MouseCursor::Defer;
  bool hovered_ = false;
};

}  // namespace fltr
