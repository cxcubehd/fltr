#pragma once

#include "fltr/core/callback.hpp"
#include "fltr/gestures/events.hpp"
#include "fltr/gestures/recognizer.hpp"

namespace fltr {

/// How far a pointer may travel and still be a tap.
inline constexpr float kTouchSlop = 18.0f;

/// Press and release without travelling far.
///
/// `onTapDown` fires when the recognizer *wins* the pointer, not when the
/// pointer goes down, so press feedback is never shown by a region that turns
/// out to have lost the gesture. With a single contender that is the same
/// instant, since the arena closes with only one member and resolves at once.
///
/// DIVERGENCE: Flutter also fires tap-down after a press timeout, so a region
/// still competing with, say, a drag recognizer can show feedback before the
/// contest ends. That needs a clock, and nothing in this layer has one -- time
/// enters the framework with the ticker. Until then a contested tap shows its
/// feedback on release.
class TapGestureRecognizer final : public GestureRecognizer {
public:
  using GestureRecognizer::GestureRecognizer;

  Callback<void()> onTapDown;
  Callback<void()> onTap;
  Callback<void()> onTapCancel;

  /// A recognizer nobody is listening to stays out of the arena, so it cannot
  /// win a pointer away from a region that would have used it.
  bool isWanted() const noexcept { return onTapDown || onTap || onTapCancel; }
  bool tracking() const noexcept { return tracking_; }

  void handleEvent(const PointerEvent& event) override;
  void acceptGesture(PointerId pointer) override;
  void rejectGesture(PointerId pointer) override;

private:
  /// This is not the tap after all: withdraw if still contending, and undo the
  /// press feedback if the contest is already over and there is no arena left to
  /// tell.
  void giveUp();
  void checkTap();
  void cancelTap();
  void reset();

  Offset downPosition_;
  PointerId primary_ = 0;
  bool tracking_ = false;
  bool won_ = false;
  bool sentDown_ = false;
  bool released_ = false;
};

}  // namespace fltr
