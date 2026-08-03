#include "fltr/gestures/drag.hpp"

#include <cmath>

namespace fltr {

float DragGestureRecognizer::slop() const noexcept {
  const bool precise = kind_ == PointerDeviceKind::Mouse;
  if (axis == DragAxis::Pan) return precise ? kPrecisePanSlop : kPanSlop;
  return precise ? kPreciseSlop : kTouchSlop;
}

float DragGestureRecognizer::primaryOf(Offset value) const noexcept {
  switch (axis) {
    case DragAxis::Horizontal:
      return value.dx;
    case DragAxis::Vertical:
      return value.dy;
    case DragAxis::Pan:
      return 0.0f;
  }
  return 0.0f;
}

float DragGestureRecognizer::magnitudeOf(Offset value) const noexcept {
  return axis == DragAxis::Pan ? value.distance() : std::fabs(primaryOf(value));
}

Offset DragGestureRecognizer::constrain(Offset value) const noexcept {
  switch (axis) {
    case DragAxis::Horizontal:
      return {value.dx, 0.0f};
    case DragAxis::Vertical:
      return {0.0f, value.dy};
    case DragAxis::Pan:
      return value;
  }
  return value;
}

void DragGestureRecognizer::handleEvent(const PointerEvent& event) {
  if (event.phase == PointerPhase::Down) {
    if (tracking_) {
      // A second pointer is no part of this drag. Give it back before the arena
      // closes, so whoever else wants it still can.
      resolve(event.pointer, GestureDisposition::Rejected);
      stopTracking(event.pointer);
      return;
    }
    tracking_ = true;
    primary_ = event.pointer;
    kind_ = event.kind;
    downPosition_ = event.position;
    lastPosition_ = event.position;
    velocity_.reset();
    velocity_.addSample(now(), event.position);
    if (onDown) onDown({event.position, toLocal(event.position)});
    return;
  }

  if (!tracking_ || event.pointer != primary_) return;

  switch (event.phase) {
    case PointerPhase::Move: {
      const Offset delta = event.position - lastPosition_;
      lastPosition_ = event.position;
      velocity_.addSample(now(), event.position);
      if (started_) {
        reportUpdate(delta, event.position);
      } else if (magnitudeOf(lastPosition_ - downPosition_) > slop()) {
        resolve(primary_, GestureDisposition::Accepted);
      }
      break;
    }
    case PointerPhase::Up: {
      if (!started_) {
        resolve(primary_, GestureDisposition::Rejected);
        reset();
        break;
      }
      const DragEndDetails details = endDetails();
      const bool fire = static_cast<bool>(onEnd);
      reset();
      if (fire) onEnd(details);
      break;
    }
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

void DragGestureRecognizer::acceptGesture(PointerId pointer) {
  if (!tracking_ || pointer != primary_ || started_) return;
  start();
}

void DragGestureRecognizer::rejectGesture(PointerId pointer) {
  if (!tracking_ || pointer != primary_) return;
  const bool fire = started_ && onCancel;
  reset();
  if (fire) onCancel();
}

/// The drag is recognized. `Start` reports it from where the pointer is now, so
/// the content does not jump by the slop distance; `Down` reports it from where
/// the pointer went down and immediately delivers the travel since, so the
/// content ends up exactly under the finger.
void DragGestureRecognizer::start() {
  started_ = true;
  const Offset travel = lastPosition_ - downPosition_;
  const Offset origin = startBehavior == DragStartBehavior::Down ? downPosition_ : lastPosition_;
  if (onStart) onStart({origin, toLocal(origin)});
  if (startBehavior == DragStartBehavior::Down && travel != Offset::zero()) {
    reportUpdate(travel, lastPosition_);
  }
}

void DragGestureRecognizer::reportUpdate(Offset delta, Offset global) {
  if (!onUpdate) return;
  // A constrained drag reports only its own axis, so a consumer never has to
  // remember to discard the other one.
  const Offset constrained = constrain(delta);
  onUpdate({constrained, primaryOf(constrained), global, toLocal(global)});
}

/// A release slower than the fling threshold is a stop, not a throw, and reports
/// no velocity at all rather than a small one the physics would still act on.
DragEndDetails DragGestureRecognizer::endDetails() const {
  const Offset estimate = velocity_.estimate().pixelsPerSecond;
  if (magnitudeOf(estimate) < minFlingVelocity) return {};
  const Offset fling =
      Velocity{estimate}.clampMagnitude(minFlingVelocity, maxFlingVelocity).pixelsPerSecond;
  return {constrain(fling), primaryOf(fling)};
}

void DragGestureRecognizer::reset() {
  if (!tracking_) return;
  stopTracking(primary_);
  tracking_ = false;
  started_ = false;
  velocity_.reset();
}

}  // namespace fltr
