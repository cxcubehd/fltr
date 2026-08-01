#pragma once

#include <memory>

#include "fltr/animation/simulation.hpp"
#include "fltr/gestures/constants.hpp"

namespace fltr {

/// Everything the physics is allowed to know about a scroll offset. Separated
/// from `ScrollPosition` so physics stays a pure policy object: it holds no
/// state, answers only from what it is handed, and can therefore be a shared
/// constant rather than something owned per scrollable.
struct ScrollMetrics {
  float pixels = 0.0f;
  float minScrollExtent = 0.0f;
  float maxScrollExtent = 0.0f;
  float viewportDimension = 0.0f;

  bool outOfRange() const noexcept { return pixels < minScrollExtent || pixels > maxScrollExtent; }
  bool canScroll() const noexcept { return maxScrollExtent > minScrollExtent; }

  float clampToRange(float value) const noexcept {
    return std::clamp(value, minScrollExtent, maxScrollExtent);
  }
  /// Signed distance outside the range: positive past the end, negative before
  /// the start, zero inside.
  float beyondRange(float value) const noexcept { return value - clampToRange(value); }

  friend constexpr bool operator==(ScrollMetrics, ScrollMetrics) noexcept = default;
};

/// The rules a scroll offset obeys, as one replaceable object.
///
/// Instances are stateless and const, so the framework ships them as shared
/// constants and a consumer that wants its own writes one and hands out a
/// pointer. Nothing allocates a physics object per scrollable.
class ScrollPhysics {
public:
  virtual ~ScrollPhysics() = default;

  /// How much of a pointer's movement becomes scroll offset. All of it, until
  /// something wants to resist past the edge.
  virtual float applyPhysicsToUserOffset(const ScrollMetrics&, float offset) const { return offset; }

  /// How much of `value` the boundary refuses, signed outward. Returning zero
  /// lets the offset leave its range, which is what makes a bounce possible.
  virtual float applyBoundaryConditions(const ScrollMetrics& metrics, float value) const {
    return metrics.beyondRange(value);
  }

  /// Null when there is nothing to animate: too slow to fling, or already at
  /// rest inside the range.
  virtual std::unique_ptr<Simulation> createBallisticSimulation(const ScrollMetrics& metrics,
                                                                float velocity) const = 0;

  virtual bool allowsUserScrolling(const ScrollMetrics& metrics) const {
    return metrics.canScroll();
  }

  Tolerance tolerance{0.1f, 5.0f};
  float minFlingVelocity = kMinFlingVelocity;
  float maxFlingVelocity = kMaxFlingVelocity;
  SpringDescription spring = SpringDescription::withDampingRatio(0.5f, 100.0f, 1.1f);

protected:
  ScrollPhysics() = default;
  ScrollPhysics(const ScrollPhysics&) = default;
  ScrollPhysics& operator=(const ScrollPhysics&) = default;
};

/// Android: the offset stops dead at the edge. What the user pushed past it is
/// refused, which is what `ScrollPosition::overscroll` then has to show.
class ClampingScrollPhysics final : public ScrollPhysics {
public:
  std::unique_ptr<Simulation> createBallisticSimulation(const ScrollMetrics& metrics,
                                                        float velocity) const override;
};

/// iOS: the offset may leave its range, resisting more the further it goes, and
/// springs back when released.
class BouncingScrollPhysics final : public ScrollPhysics {
public:
  float applyPhysicsToUserOffset(const ScrollMetrics& metrics, float offset) const override;
  float applyBoundaryConditions(const ScrollMetrics&, float) const override { return 0.0f; }
  std::unique_ptr<Simulation> createBallisticSimulation(const ScrollMetrics& metrics,
                                                        float velocity) const override;
};

/// Refuses to move at all, which is how a scroll view is disabled without
/// removing it or losing where it was.
class NeverScrollPhysics final : public ScrollPhysics {
public:
  bool allowsUserScrolling(const ScrollMetrics&) const override { return false; }
  std::unique_ptr<Simulation> createBallisticSimulation(const ScrollMetrics&,
                                                        float) const override {
    return nullptr;
  }
};

/// The default when a scrollable is given none.
const ScrollPhysics& defaultScrollPhysics() noexcept;

}  // namespace fltr
