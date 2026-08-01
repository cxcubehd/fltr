#include "fltr/scroll/physics.hpp"

namespace fltr {

std::unique_ptr<Simulation> ClampingScrollPhysics::createBallisticSimulation(
    const ScrollMetrics& metrics, float velocity) const {
  if (metrics.outOfRange()) {
    // Only reachable when something else moved the offset out -- content
    // shrinking under a scrolled-down view. Never carry velocity outward.
    return std::make_unique<SpringSimulation>(spring, metrics.pixels,
                                              metrics.clampToRange(metrics.pixels),
                                              std::min(0.0f, velocity), tolerance);
  }
  if (std::fabs(velocity) < tolerance.velocity) return nullptr;
  if (velocity > 0.0f && metrics.pixels >= metrics.maxScrollExtent) return nullptr;
  if (velocity < 0.0f && metrics.pixels <= metrics.minScrollExtent) return nullptr;
  return std::make_unique<ClampingScrollSimulation>(metrics.pixels, velocity, 0.015f, tolerance);
}

namespace {

/// How much of a further push survives, given how far out we already are. It
/// falls off with the square of the fraction of a viewport overscrolled, so the
/// edge feels progressively stiffer rather than simply slower.
float frictionFactor(float overscrollFraction) noexcept {
  const float remaining = 1.0f - overscrollFraction;
  return 0.52f * remaining * remaining;
}

/// Applies `factor` only to the part of the push that happens outside the
/// range: a drag that crosses the edge is at full strength up to it and damped
/// after, rather than damped along its whole length.
float applyFriction(float extentOutside, float absDelta, float factor) noexcept {
  if (extentOutside <= 0.0f) return absDelta;
  const float deltaToLimit = extentOutside / factor;
  if (absDelta < deltaToLimit) return absDelta * factor;
  return extentOutside + (absDelta - deltaToLimit);
}

}  // namespace

float BouncingScrollPhysics::applyPhysicsToUserOffset(const ScrollMetrics& metrics,
                                                      float offset) const {
  if (!metrics.outOfRange() || metrics.viewportDimension <= 0.0f) return offset;

  const float pastStart = std::max(metrics.minScrollExtent - metrics.pixels, 0.0f);
  const float pastEnd = std::max(metrics.pixels - metrics.maxScrollExtent, 0.0f);
  const float outside = std::max(pastStart, pastEnd);

  // Coming back costs nothing extra: the friction is measured from where the
  // push will leave us, not from where it started.
  const bool returning = (pastStart > 0.0f && offset < 0.0f) || (pastEnd > 0.0f && offset > 0.0f);
  const float measured = returning ? outside - std::fabs(offset) : outside;
  const float factor = frictionFactor(std::clamp(measured / metrics.viewportDimension, 0.0f, 1.0f));
  return std::copysign(applyFriction(outside, std::fabs(offset), factor), offset);
}

std::unique_ptr<Simulation> BouncingScrollPhysics::createBallisticSimulation(
    const ScrollMetrics& metrics, float velocity) const {
  if (std::fabs(velocity) < tolerance.velocity && !metrics.outOfRange()) return nullptr;
  return std::make_unique<BouncingScrollSimulation>(metrics.pixels, velocity,
                                                    metrics.minScrollExtent,
                                                    metrics.maxScrollExtent, spring, tolerance);
}

const ScrollPhysics& defaultScrollPhysics() noexcept {
  static const ClampingScrollPhysics physics;
  return physics;
}

}  // namespace fltr
