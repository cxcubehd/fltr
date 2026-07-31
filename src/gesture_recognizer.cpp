#include "fltr/gestures/recognizer.hpp"

#include "fltr/gestures/tap.hpp"

namespace fltr {

GestureRecognizer::~GestureRecognizer() {
  // Routes first: nothing may reach an object whose derived half is already
  // gone. Withdrawing from the arena calls nothing back, by design.
  binding_->removeRoutes(*this);
  binding_->arena().remove(*this);
}

void GestureRecognizer::addPointer(const PointerEvent& down) {
  FLTR_EXPECTS(down.phase == PointerPhase::Down, "addPointer expects a pointer-down event");
  binding_->addRoute(down.pointer, *this);
  binding_->arena().add(down.pointer, *this);
  handleEvent(down);
}

void GestureRecognizer::stopTracking(PointerId pointer) {
  binding_->removeRoute(pointer, *this);
}

void GestureRecognizer::resolve(PointerId pointer, GestureDisposition disposition) {
  binding_->arena().resolve(pointer, *this, disposition);
}

// ---------------------------------------------------------------------------
// TapGestureRecognizer
// ---------------------------------------------------------------------------

void TapGestureRecognizer::handleEvent(const PointerEvent& event) {
  if (event.phase == PointerPhase::Down) {
    if (tracking_) {
      // A second pointer is no part of this tap. Give it back before the arena
      // closes, so whoever else wants it still can.
      resolve(event.pointer, GestureDisposition::Rejected);
      stopTracking(event.pointer);
      return;
    }
    tracking_ = true;
    primary_ = event.pointer;
    downPosition_ = event.position;
    return;
  }

  if (!tracking_ || event.pointer != primary_) return;

  switch (event.phase) {
    case PointerPhase::Move:
      if ((event.position - downPosition_).distance() > kTouchSlop) giveUp();
      break;
    case PointerPhase::Up:
      released_ = true;
      checkTap();
      break;
    case PointerPhase::Cancel:
      giveUp();
      break;
    case PointerPhase::Down:
    case PointerPhase::Hover:
      break;
  }
}

void TapGestureRecognizer::acceptGesture(PointerId pointer) {
  if (!tracking_ || pointer != primary_) return;
  won_ = true;
  if (!sentDown_) {
    sentDown_ = true;
    if (onTapDown) onTapDown();
  }
  checkTap();
}

void TapGestureRecognizer::rejectGesture(PointerId pointer) {
  if (!tracking_ || pointer != primary_) return;
  cancelTap();
  reset();
}

void TapGestureRecognizer::giveUp() {
  if (won_) {
    cancelTap();
    reset();
  } else {
    resolve(primary_, GestureDisposition::Rejected);
  }
}

void TapGestureRecognizer::checkTap() {
  if (!won_ || !released_) return;
  const bool fire = static_cast<bool>(onTap);
  reset();
  if (fire) onTap();
}

void TapGestureRecognizer::cancelTap() {
  if (sentDown_ && onTapCancel) onTapCancel();
}

void TapGestureRecognizer::reset() {
  if (tracking_) stopTracking(primary_);
  tracking_ = false;
  won_ = false;
  sentDown_ = false;
  released_ = false;
}

}  // namespace fltr
