#include "fltr/gestures/velocity.hpp"

#include <cmath>

namespace fltr {
namespace {

/// A degree-2 least-squares fit of `y` against `x`, returning the slope at
/// x = 0 and how much of the variance the fit explains. The normal equations for
/// three coefficients are a 3x3 symmetric system, small enough to solve by
/// cofactors and well conditioned because x is a duration under a tenth of a
/// second.
struct Fit {
  double slope = 0.0;
  double confidence = 0.0;
  bool ok = false;
};

Fit fitQuadratic(const double* x, const double* y, std::size_t n) {
  double s0 = static_cast<double>(n), s1 = 0, s2 = 0, s3 = 0, s4 = 0;
  double t0 = 0, t1 = 0, t2 = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double a = x[i], a2 = a * a;
    s1 += a;
    s2 += a2;
    s3 += a2 * a;
    s4 += a2 * a2;
    t0 += y[i];
    t1 += a * y[i];
    t2 += a2 * y[i];
  }

  const double c00 = s2 * s4 - s3 * s3;
  const double c01 = s2 * s3 - s1 * s4;
  const double c02 = s1 * s3 - s2 * s2;
  const double det = s0 * c00 + s1 * c01 + s2 * c02;
  if (std::fabs(det) < 1e-12) return {};

  const double c11 = s0 * s4 - s2 * s2;
  const double c12 = s1 * s2 - s0 * s3;
  const double c22 = s0 * s2 - s1 * s1;
  const double inverseDet = 1.0 / det;
  const double b0 = (c00 * t0 + c01 * t1 + c02 * t2) * inverseDet;
  const double b1 = (c01 * t0 + c11 * t1 + c12 * t2) * inverseDet;
  const double b2 = (c02 * t0 + c12 * t1 + c22 * t2) * inverseDet;

  const double mean = t0 / static_cast<double>(n);
  double residual = 0, total = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double predicted = b0 + b1 * x[i] + b2 * x[i] * x[i];
    residual += (y[i] - predicted) * (y[i] - predicted);
    total += (y[i] - mean) * (y[i] - mean);
  }
  const double confidence = total <= 0.0 ? 1.0 : 1.0 - residual / total;
  return {b1, confidence < 0.0 ? 0.0 : confidence, true};
}

}  // namespace

Velocity Velocity::clampMagnitude(float minimum, float maximum) const noexcept {
  const float speed = pixelsPerSecond.distance();
  if (speed <= 0.0f) return *this;
  if (speed < minimum) return {pixelsPerSecond * (minimum / speed)};
  if (speed > maximum) return {pixelsPerSecond * (maximum / speed)};
  return *this;
}

void VelocityTracker::addSample(float time, Offset position) {
  if (count_ > 0) {
    Sample& last = samples_[(next_ + kCapacity - 1) % kCapacity];
    if (last.time == time) {
      last.position = position;
      return;
    }
  }
  samples_[next_] = {time, position};
  next_ = (next_ + 1) % kCapacity;
  if (count_ < kCapacity) ++count_;
}

VelocityEstimate VelocityTracker::estimate() const {
  if (count_ < 2) return {};

  double time[kCapacity], px[kCapacity], py[kCapacity];
  const Sample& newest = samples_[(next_ + kCapacity - 1) % kCapacity];
  std::size_t n = 0;
  for (std::size_t i = 0; i < count_; ++i) {
    const Sample& s = samples_[(next_ + kCapacity - 1 - i) % kCapacity];
    if (newest.time - s.time > kHorizon) break;
    // Newest first and at x = 0, so the fitted slope there is the velocity at
    // the moment of release rather than an average over the window.
    time[n] = static_cast<double>(s.time - newest.time);
    px[n] = static_cast<double>(s.position.dx);
    py[n] = static_cast<double>(s.position.dy);
    ++n;
  }
  if (n < 2) return {};

  const Sample& oldest = samples_[(next_ + kCapacity - n) % kCapacity];
  VelocityEstimate result;
  result.duration = newest.time - oldest.time;
  result.offset = newest.position - oldest.position;
  if (result.duration <= 0.0f) return result;

  if (n >= 3) {
    const Fit x = fitQuadratic(time, px, n);
    const Fit y = fitQuadratic(time, py, n);
    if (x.ok && y.ok) {
      result.pixelsPerSecond = {static_cast<float>(x.slope), static_cast<float>(y.slope)};
      result.confidence = static_cast<float>(x.confidence * y.confidence);
      return result;
    }
  }
  // Two samples, or a degenerate fit: the secant over the window is the only
  // thing the evidence supports.
  result.pixelsPerSecond = result.offset * (1.0f / result.duration);
  result.confidence = 1.0f;
  return result;
}

}  // namespace fltr
