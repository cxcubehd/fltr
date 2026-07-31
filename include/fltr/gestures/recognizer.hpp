#pragma once

#include "fltr/gestures/arena.hpp"
#include "fltr/gestures/binding.hpp"
#include "fltr/gestures/events.hpp"

namespace fltr {

/// Turns a stream of pointer events into a gesture, competing for the pointer
/// with every other recognizer under the same point.
///
/// A recognizer is owned by the render object that offered it the pointer, so
/// its lifetime is unambiguous and its routes and arena entries are withdrawn
/// when it dies.
class GestureRecognizer : public GestureArenaMember {
public:
  explicit GestureRecognizer(PointerBinding& binding) noexcept : binding_(&binding) {}
  virtual ~GestureRecognizer();

  GestureRecognizer(const GestureRecognizer&) = delete;
  GestureRecognizer& operator=(const GestureRecognizer&) = delete;

  /// Enters the arena for this pointer and starts receiving its events. Called
  /// as the down event travels the hit-test path.
  void addPointer(const PointerEvent& down);

  /// Every event of a pointer this recognizer is following, including the down
  /// that started it, and including events delivered after the pointer has left
  /// the region it started in.
  virtual void handleEvent(const PointerEvent& event) = 0;

protected:
  void stopTracking(PointerId pointer);
  void resolve(PointerId pointer, GestureDisposition disposition);

  PointerBinding& binding() const noexcept { return *binding_; }

private:
  PointerBinding* binding_;
};

}  // namespace fltr
