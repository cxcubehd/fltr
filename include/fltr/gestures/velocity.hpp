#pragma once

#include <cstddef>

#include "fltr/core/geometry.hpp"

namespace fltr {

struct Velocity {
  Offset pixelsPerSecond;

  Velocity clampMagnitude(float minimum, float maximum) const noexcept;
};

struct VelocityEstimate {
  Offset pixelsPerSecond;
  /// How well the fit explains the samples, 0 to 1. A gesture that changed
  /// direction mid-flight scores low, which is the signal to distrust it.
  float confidence = 0.0f;
  /// Seconds the samples used span, and how far the pointer moved over them.
  float duration = 0.0f;
  Offset offset;
};

/// Estimates how fast a pointer was moving when it was released.
///
/// A least-squares quadratic fit over a bounded window, as Flutter does, rather
/// than the difference between the last two samples: a finger decelerating into
/// a release produces a large final delta and a fling that does not match what
/// the hand did.
class VelocityTracker {
public:
  /// `time` is the gesture clock the binding advances, so several samples may
  /// share one value -- a game pumps its input queue at a frame boundary. A
  /// repeated time replaces the sample rather than adding a second one at the
  /// same instant, which would weight that instant twice in the fit.
  void addSample(float time, Offset position);

  VelocityEstimate estimate() const;
  Velocity velocity() const { return {estimate().pixelsPerSecond}; }

  void reset() noexcept { count_ = 0; }
  std::size_t sampleCount() const noexcept { return count_; }

private:
  static constexpr std::size_t kCapacity = 20;
  /// Samples older than this before the newest are not evidence about the
  /// release; Flutter's window, for the same reason.
  static constexpr float kHorizon = 0.1f;

  struct Sample {
    float time = 0.0f;
    Offset position;
  };

  /// A ring, so a long drag neither grows nor shifts anything.
  Sample samples_[kCapacity]{};
  std::size_t next_ = 0;
  std::size_t count_ = 0;
};

}  // namespace fltr
