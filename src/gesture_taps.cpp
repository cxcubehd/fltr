#include "fltr/gestures/long_press.hpp"
#include "fltr/gestures/multitap.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// LongPressGestureRecognizer
// ---------------------------------------------------------------------------

void LongPressGestureRecognizer::handleEvent(const PointerEvent& event) {
  if (event.phase == PointerPhase::Down) {
    if (tracking_) {
      resolve(event.pointer, GestureDisposition::Rejected);
      stopTracking(event.pointer);
      return;
    }
    tracking_ = true;
    primary_ = event.pointer;
    downPosition_ = event.position;
    velocity_.reset();
    velocity_.addSample(now(), event.position);
    scheduleTimeout(event.pointer, timeout);
    return;
  }

  if (!tracking_ || event.pointer != primary_) return;

  switch (event.phase) {
    case PointerPhase::Move:
      velocity_.addSample(now(), event.position);
      if (started_) {
        if (onMoveUpdate) {
          onMoveUpdate({event.position, toLocal(event.position), event.position - downPosition_});
        }
      } else if ((event.position - downPosition_).distance() > slop) {
        // Drifted before the deadline: this was never a long press.
        resolve(primary_, GestureDisposition::Rejected);
        reset();
      }
      break;
    case PointerPhase::Up:
      if (started_) {
        const Offset position = event.position;
        const Offset released = velocity_.velocity().pixelsPerSecond;
        const bool fire = static_cast<bool>(onEnd);
        reset();
        if (fire) onEnd({position, toLocal(position), released});
      } else {
        resolve(primary_, GestureDisposition::Rejected);
        reset();
      }
      break;
    case PointerPhase::Cancel: {
      const bool fire = started_ && onCancel;
      resolve(primary_, GestureDisposition::Rejected);
      reset();
      if (fire) onCancel();
      break;
    }
    case PointerPhase::Down:
    case PointerPhase::Hover:
      break;
  }
}

/// The deadline, not the arena, is what starts a long press: with no other
/// contender the arena awards this recognizer the instant the pointer goes down,
/// which is the one moment a long press has certainly not happened yet.
void LongPressGestureRecognizer::handleTimeout(PointerId pointer) {
  if (!tracking_ || pointer != primary_ || started_) return;
  started_ = true;
  resolve(primary_, GestureDisposition::Accepted);
  // Rejecting the others can destroy this gesture from a callback.
  if (started_ && onStart) onStart({downPosition_, toLocal(downPosition_)});
}

void LongPressGestureRecognizer::rejectGesture(PointerId pointer) {
  if (!tracking_ || pointer != primary_) return;
  const bool fire = started_ && onCancel;
  reset();
  if (fire) onCancel();
}

void LongPressGestureRecognizer::reset() {
  if (!tracking_) return;
  stopTracking(primary_);
  cancelTimeouts();
  tracking_ = false;
  started_ = false;
  velocity_.reset();
}

// ---------------------------------------------------------------------------
// DoubleTapGestureRecognizer
// ---------------------------------------------------------------------------

void DoubleTapGestureRecognizer::handleEvent(const PointerEvent& event) {
  if (event.phase == PointerPhase::Down) {
    if (!firstTracked_) {
      firstTracked_ = true;
      firstPointer_ = event.pointer;
      firstPosition_ = event.position;
      return;
    }
    if (firstUp_ && !secondTracked_ && (event.position - firstPosition_).distance() <= slop) {
      secondTracked_ = true;
      secondPointer_ = event.pointer;
      secondPosition_ = event.position;
      // Winning the first pointer's arena is what stops the tap that has been
      // waiting on the hold from firing as a single tap.
      cancelTimeouts();
      resolve(firstPointer_, GestureDisposition::Accepted);
      return;
    }
    resolve(event.pointer, GestureDisposition::Rejected);
    stopTracking(event.pointer);
    return;
  }

  const bool isFirst = firstTracked_ && event.pointer == firstPointer_;
  const bool isSecond = secondTracked_ && event.pointer == secondPointer_;
  if (!isFirst && !isSecond) return;

  switch (event.phase) {
    case PointerPhase::Move:
      // Movement within one tap is judged by the ordinary touch slop; the wider
      // `slop` is how far apart the two taps may land, which is a different
      // question and a much larger distance.
      if ((event.position - (isSecond ? secondPosition_ : firstPosition_)).distance() >
          kTouchSlop) {
        reset();
      }
      break;
    case PointerPhase::Up:
      if (isSecond) {
        const bool fire = static_cast<bool>(onDoubleTap);
        resolve(secondPointer_, GestureDisposition::Accepted);
        reset();
        if (fire) onDoubleTap();
      } else if (!firstUp_) {
        firstUp_ = true;
        // The sweep this up is about to trigger would award the pointer to
        // whoever else is contending. Hold it until the wait for a second tap is
        // over, one way or the other.
        binding().arena().hold(firstPointer_);
        scheduleTimeout(firstPointer_, timeout);
      }
      break;
    case PointerPhase::Cancel:
      reset();
      break;
    case PointerPhase::Down:
    case PointerPhase::Hover:
      break;
  }
}

void DoubleTapGestureRecognizer::handleTimeout(PointerId pointer) {
  if (firstTracked_ && pointer == firstPointer_) reset();
}

void DoubleTapGestureRecognizer::rejectGesture(PointerId pointer) {
  if ((firstTracked_ && pointer == firstPointer_) || (secondTracked_ && pointer == secondPointer_)) {
    reset();
  }
}

void DoubleTapGestureRecognizer::reset() {
  cancelTimeouts();
  // Both pointers are given up before either is resolved, so a rejection that
  // comes straight back through `rejectGesture` finds nothing left to reset.
  const bool hadFirst = firstTracked_;
  const bool hadSecond = secondTracked_;
  const PointerId first = firstPointer_;
  const PointerId second = secondPointer_;
  firstTracked_ = false;
  secondTracked_ = false;
  firstUp_ = false;

  if (hadFirst) {
    stopTracking(first);
    resolve(first, GestureDisposition::Rejected);
    // Whatever the deferred sweep was going to award, award it now.
    binding().arena().release(first);
  }
  if (hadSecond) {
    stopTracking(second);
    resolve(second, GestureDisposition::Rejected);
  }
}

}  // namespace fltr
