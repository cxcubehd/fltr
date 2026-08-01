#pragma once

#include "fltr/core/callback.hpp"
#include "fltr/gestures/constants.hpp"
#include "fltr/gestures/recognizer.hpp"
#include "fltr/gestures/velocity.hpp"

namespace fltr {

struct LongPressStartDetails {
  Offset globalPosition;
  Offset localPosition;
};

struct LongPressMoveUpdateDetails {
  Offset globalPosition;
  Offset localPosition;
  Offset offsetFromOrigin;
};

struct LongPressEndDetails {
  Offset globalPosition;
  Offset localPosition;
  Offset velocity;
};

/// A press held in place long enough to mean something other than a tap.
///
/// It claims the pointer when the deadline passes, which is what rejects the tap
/// competing for it. Until then it contends without having committed, so lifting
/// early leaves the tap to win normally.
class LongPressGestureRecognizer final : public GestureRecognizer {
public:
  using GestureRecognizer::GestureRecognizer;

  float timeout = kLongPressTimeout;
  /// How far the pointer may drift before the press is abandoned. Travel after
  /// it has been recognized is reported rather than cancelling it.
  float slop = kTouchSlop;

  Callback<void(const LongPressStartDetails&)> onStart;
  Callback<void(const LongPressMoveUpdateDetails&)> onMoveUpdate;
  Callback<void(const LongPressEndDetails&)> onEnd;
  Callback<void()> onCancel;

  bool isWanted() const noexcept { return onStart || onMoveUpdate || onEnd || onCancel; }
  bool tracking() const noexcept { return tracking_; }
  bool pressing() const noexcept { return started_; }

  void handleEvent(const PointerEvent& event) override;
  void handleTimeout(PointerId pointer) override;
  void acceptGesture(PointerId) override {}
  void rejectGesture(PointerId pointer) override;

private:
  void reset();

  VelocityTracker velocity_;
  Offset downPosition_;
  PointerId primary_ = 0;
  bool tracking_ = false;
  bool started_ = false;
};

}  // namespace fltr
