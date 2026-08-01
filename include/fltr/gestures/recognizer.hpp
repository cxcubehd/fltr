#pragma once

#include "fltr/gestures/arena.hpp"
#include "fltr/gestures/binding.hpp"
#include "fltr/gestures/events.hpp"

namespace fltr {

class RenderBox;

/// Turns a stream of pointer events into a gesture, competing for the pointer
/// with every other recognizer under the same point.
///
/// A recognizer is owned by the render object that offered it the pointer, so
/// its lifetime is unambiguous and its routes, arena entries and timeouts are
/// withdrawn when it dies.
class GestureRecognizer : public GestureArenaMember {
public:
  explicit GestureRecognizer(PointerBinding& binding) noexcept : binding_(&binding) {}
  virtual ~GestureRecognizer();

  GestureRecognizer(const GestureRecognizer&) = delete;
  GestureRecognizer& operator=(const GestureRecognizer&) = delete;

  /// Which buttons this recognizer answers to. A down holding none of them is
  /// not offered to it at all, which is how a secondary-button menu coexists
  /// with a primary-button tap on the same region.
  PointerButtons allowedButtons{PointerButton::Primary};

  /// The box whose local space this recognizer reports positions in; the region
  /// that owns it sets this. Without one, local and global agree.
  void setCoordinateSpace(RenderBox* box) noexcept { space_ = box; }

  /// Enters the arena for this pointer and starts receiving its events. Called
  /// as the down event travels the hit-test path, and ignored for a button this
  /// recognizer does not answer to.
  void addPointer(const PointerEvent& down);

  /// Every event of a pointer this recognizer is following, including the down
  /// that started it, and including events delivered after the pointer has left
  /// the region it started in.
  virtual void handleEvent(const PointerEvent& event) = 0;

  /// A deadline this recognizer set has passed. Deadlines are measured on the
  /// gesture clock, so they land on a frame boundary rather than at an arbitrary
  /// instant -- the same bargain the ticker makes.
  virtual void handleTimeout(PointerId /*pointer*/) {}

protected:
  void stopTracking(PointerId pointer);
  void resolve(PointerId pointer, GestureDisposition disposition);

  void scheduleTimeout(PointerId pointer, float delay) {
    binding_->scheduleTimeout(*this, pointer, delay);
  }
  void cancelTimeouts() { binding_->cancelTimeouts(*this); }
  float now() const noexcept { return binding_->now(); }

  Offset toLocal(Offset global) const;

  PointerBinding& binding() const noexcept { return *binding_; }

private:
  PointerBinding* binding_;
  RenderBox* space_ = nullptr;
};

}  // namespace fltr
