#pragma once

#include "fltr/core/callback.hpp"
#include "fltr/gestures/constants.hpp"
#include "fltr/gestures/recognizer.hpp"

namespace fltr {

/// Two taps in the same place in quick succession.
///
/// The awkward part is not the second tap, it is the first: when it comes up,
/// the arena would sweep and award the pointer to whoever else is contending --
/// a plain tap, usually -- before this recognizer knows whether a second tap is
/// coming. So it *holds* the arena across that gap and releases it when the wait
/// ends, which is why a region with both a tap and a double tap fires its single
/// tap one timeout late. Flutter has exactly this behaviour, for exactly this
/// reason.
class DoubleTapGestureRecognizer final : public GestureRecognizer {
public:
  using GestureRecognizer::GestureRecognizer;

  float timeout = kDoubleTapTimeout;
  /// How far apart the two taps may land.
  float slop = kDoubleTapSlop;

  Callback<void()> onDoubleTap;

  bool isWanted() const noexcept { return static_cast<bool>(onDoubleTap); }
  bool awaitingSecondTap() const noexcept { return firstUp_; }

  void handleEvent(const PointerEvent& event) override;
  void handleTimeout(PointerId pointer) override;
  /// Winning is not what completes a double tap; the second tap is. With no
  /// other contender the arena awards the first pointer immediately, which says
  /// nothing about whether a second tap is coming.
  void acceptGesture(PointerId) override {}
  void rejectGesture(PointerId pointer) override;

private:
  /// Gives back both pointers and lets the arena resolve whatever was waiting on
  /// the hold.
  void reset();

  Offset firstPosition_;
  Offset secondPosition_;
  PointerId firstPointer_ = 0;
  PointerId secondPointer_ = 0;
  bool firstTracked_ = false;
  bool firstUp_ = false;
  bool secondTracked_ = false;
};

}  // namespace fltr
