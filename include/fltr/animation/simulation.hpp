#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

#include "fltr/core/geometry.hpp"

namespace fltr {

/// How close is close enough to stop. Every simulation here settles
/// asymptotically, so "done" is a tolerance rather than an equality.
struct Tolerance {
  float distance = 0.01f;
  float velocity = 0.01f;
};

/// Position and velocity as functions of time, plus when to stop asking.
///
/// `AnimationDriver` is normalized 0..1 over a duration; this is unbounded in
/// value and ends by tolerance, so it stands alongside the driver rather than
/// replacing it. Nothing here knows what the number means -- scrolling reads it
/// as pixels, and a spring-based implicit animation could read it as anything.
class Simulation {
public:
  explicit Simulation(Tolerance settling = {}) noexcept : tolerance_(settling) {}
  virtual ~Simulation() = default;

  virtual float x(float time) const = 0;
  virtual float dx(float time) const = 0;
  virtual bool isDone(float time) const = 0;

protected:
  Simulation(const Simulation&) = default;
  Simulation& operator=(const Simulation&) = default;

  Tolerance tolerance_;
};

/// Velocity decaying exponentially, by a factor of `drag` per second. This is
/// the iOS fling, and the first half of the bouncing one.
class FrictionSimulation final : public Simulation {
public:
  FrictionSimulation(float drag, float position, float velocity, Tolerance settling = {}) noexcept;

  float x(float time) const override;
  float dx(float time) const override;
  bool isDone(float time) const override { return std::fabs(dx(time)) < tolerance_.velocity; }

  /// Where it comes to rest. Reachable only in the limit, which is why the
  /// bouncing simulation asks for it rather than integrating to find out.
  float finalX() const noexcept { return position_ - velocity_ / dragLog_; }
  /// When it passes `value`, or infinity if it never does.
  float timeAtX(float value) const noexcept;

private:
  float drag_;
  float position_;
  float velocity_;
  float dragLog_;
};

struct SpringDescription {
  float mass = 1.0f;
  float stiffness = 100.0f;
  float damping = 20.0f;

  /// A ratio of one is critically damped -- the fastest approach that does not
  /// overshoot. Below one overshoots and rings; above one crawls in.
  static SpringDescription withDampingRatio(float mass, float stiffness,
                                            float ratio = 1.0f) noexcept;
};

/// A mass on a spring, solved in closed form rather than integrated, so a frame
/// of it is a handful of transcendentals and no accumulated error.
class SpringSimulation final : public Simulation {
public:
  SpringSimulation(SpringDescription spring, float start, float end, float velocity,
                   Tolerance settling = {}) noexcept;

  float x(float time) const override { return end_ + displacement(time); }
  float dx(float time) const override;
  bool isDone(float time) const override {
    return std::fabs(displacement(time)) < tolerance_.distance &&
           std::fabs(dx(time)) < tolerance_.velocity;
  }

private:
  enum class Damping : std::uint8_t { Under, Critical, Over };

  float displacement(float time) const;

  float end_;
  Damping damping_;
  /// The two exponents of the overdamped solution; for the underdamped one,
  /// the decay rate and the ringing frequency, and for the critical one only
  /// the first is used.
  float r1_ = 0.0f;
  float r2_ = 0.0f;
  float c1_ = 0.0f;
  float c2_ = 0.0f;
};

/// Android's fling: a power-law deceleration over a fixed duration, so it
/// arrives and stops instead of approaching forever.
class ClampingScrollSimulation final : public Simulation {
public:
  ClampingScrollSimulation(float position, float velocity, float friction = 0.015f,
                           Tolerance settling = {}) noexcept;

  float x(float time) const override;
  float dx(float time) const override;
  bool isDone(float time) const override { return time >= duration_; }

private:
  float position_;
  float velocity_;
  float duration_;
  float distance_;
};

/// iOS's fling: friction until it leaves the range, then a spring that carries
/// it past the edge and pulls it back.
class BouncingScrollSimulation final : public Simulation {
public:
  BouncingScrollSimulation(float position, float velocity, float leadingExtent,
                           float trailingExtent, SpringDescription spring,
                           Tolerance settling = {}) noexcept;

  float x(float time) const override;
  float dx(float time) const override;
  bool isDone(float time) const override;

private:
  /// Which half is in charge at `time`, and the time to ask it about -- the
  /// spring's clock starts when the friction handed over.
  const Simulation& phaseAt(float time, float& local) const;

  std::optional<FrictionSimulation> friction_;
  std::optional<SpringSimulation> spring_;
  /// When the friction reaches the edge. Positive infinity means it never does
  /// and negative infinity means the position was already outside.
  float springTime_ = kInf;
};

}  // namespace fltr
