#pragma once

#include "fltr/core/callback.hpp"
#include "fltr/gestures/constants.hpp"
#include "fltr/gestures/recognizer.hpp"
#include "fltr/gestures/velocity.hpp"

namespace fltr {

/// DIVERGENCE: Flutter has three recognizer classes -- vertical, horizontal and
/// pan -- where this is one recognizer with an axis. The arena behaviour is
/// identical, since what differs between them is only which distance is compared
/// against which slop, and one class is a great deal less to instantiate from a
/// widget that has to pick at runtime.
enum class DragAxis : std::uint8_t { Pan, Horizontal, Vertical };

/// Where a drag is measured from once it is recognized.
enum class DragStartBehavior : std::uint8_t {
  /// The point at which the slop was cleared. The content does not jump by the
  /// slop distance, which is what a scroll view wants.
  Start,
  /// The point at which the pointer went down. The content keeps up with the
  /// finger exactly, which is what dragging an object wants.
  Down,
};

struct DragDownDetails {
  Offset globalPosition;
  Offset localPosition;
};

struct DragStartDetails {
  Offset globalPosition;
  Offset localPosition;
};

struct DragUpdateDetails {
  Offset delta;
  /// The component of `delta` along a constrained axis; zero for a pan, which
  /// has no primary axis to report.
  float primaryDelta = 0.0f;
  Offset globalPosition;
  Offset localPosition;
};

struct DragEndDetails {
  Offset velocity;
  float primaryVelocity = 0.0f;
};

/// A press that travels far enough to be a drag rather than a tap.
///
/// It does not enter the arena as a winner: it competes until the pointer has
/// moved past the slop on its axis, which is what lets a tap inside a scrollable
/// still be a tap.
class DragGestureRecognizer final : public GestureRecognizer {
public:
  using GestureRecognizer::GestureRecognizer;

  DragAxis axis = DragAxis::Pan;
  DragStartBehavior startBehavior = DragStartBehavior::Start;
  float minFlingVelocity = kMinFlingVelocity;
  float maxFlingVelocity = kMaxFlingVelocity;

  Callback<void(const DragDownDetails&)> onDown;
  Callback<void(const DragStartDetails&)> onStart;
  Callback<void(const DragUpdateDetails&)> onUpdate;
  Callback<void(const DragEndDetails&)> onEnd;
  Callback<void()> onCancel;

  bool isWanted() const noexcept { return onDown || onStart || onUpdate || onEnd || onCancel; }
  bool tracking() const noexcept { return tracking_; }
  bool dragging() const noexcept { return started_; }

  void handleEvent(const PointerEvent& event) override;
  void acceptGesture(PointerId pointer) override;
  void rejectGesture(PointerId pointer) override;

private:
  /// How far this pointer has to travel to be a drag, which is a question about
  /// the device as much as about the axis.
  float slop() const noexcept;
  /// The component of an offset this recognizer measures: the whole magnitude
  /// for a pan, and only the axis for a constrained drag.
  float primaryOf(Offset value) const noexcept;
  float magnitudeOf(Offset value) const noexcept;
  Offset constrain(Offset value) const noexcept;
  DragEndDetails endDetails() const;
  void start();
  void reportUpdate(Offset delta, Offset global);
  void reset();

  VelocityTracker velocity_;
  Offset downPosition_;
  Offset lastPosition_;
  PointerDeviceKind kind_ = PointerDeviceKind::Touch;
  PointerId primary_ = 0;
  bool tracking_ = false;
  bool started_ = false;
};

}  // namespace fltr
