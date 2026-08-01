#include "fltr/animation/simulation.hpp"

#include <algorithm>

namespace fltr {

// ---------------------------------------------------------------------------
// FrictionSimulation
// ---------------------------------------------------------------------------

FrictionSimulation::FrictionSimulation(float drag, float position, float velocity,
                                       Tolerance settling) noexcept
    : Simulation(settling),
      drag_(drag),
      position_(position),
      velocity_(velocity),
      dragLog_(std::log(drag)) {}

float FrictionSimulation::x(float time) const {
  return position_ + velocity_ * std::pow(drag_, time) / dragLog_ - velocity_ / dragLog_;
}

float FrictionSimulation::dx(float time) const { return velocity_ * std::pow(drag_, time); }

float FrictionSimulation::timeAtX(float value) const noexcept {
  if (value == position_) return 0.0f;
  const float rest = finalX();
  const bool reachable = velocity_ > 0.0f ? (value > position_ && value <= rest)
                                          : (value < position_ && value >= rest);
  if (velocity_ == 0.0f || !reachable) return kInf;
  return std::log(dragLog_ * (value - position_) / velocity_ + 1.0f) / dragLog_;
}

// ---------------------------------------------------------------------------
// SpringSimulation
// ---------------------------------------------------------------------------

SpringDescription SpringDescription::withDampingRatio(float mass, float stiffness,
                                                      float ratio) noexcept {
  return {mass, stiffness, ratio * 2.0f * std::sqrt(mass * stiffness)};
}

SpringSimulation::SpringSimulation(SpringDescription spring, float start, float end, float velocity,
                                   Tolerance settling) noexcept
    : Simulation(settling), end_(end) {
  const float displacement = start - end;
  const float discriminant = spring.damping * spring.damping - 4.0f * spring.mass * spring.stiffness;
  if (discriminant > 0.0f) {
    damping_ = Damping::Over;
    const float root = std::sqrt(discriminant);
    r1_ = (-spring.damping - root) / (2.0f * spring.mass);
    r2_ = (-spring.damping + root) / (2.0f * spring.mass);
    c2_ = (velocity - r1_ * displacement) / (r2_ - r1_);
    c1_ = displacement - c2_;
  } else if (discriminant == 0.0f) {
    damping_ = Damping::Critical;
    r1_ = -spring.damping / (2.0f * spring.mass);
    c1_ = displacement;
    c2_ = velocity - r1_ * displacement;
  } else {
    damping_ = Damping::Under;
    r1_ = -spring.damping / (2.0f * spring.mass);
    r2_ = std::sqrt(4.0f * spring.mass * spring.stiffness - spring.damping * spring.damping) /
          (2.0f * spring.mass);
    c1_ = displacement;
    c2_ = (velocity - r1_ * displacement) / r2_;
  }
}

float SpringSimulation::displacement(float time) const {
  const float decay = std::exp(r1_ * time);
  switch (damping_) {
    case Damping::Over:
      return c1_ * decay + c2_ * std::exp(r2_ * time);
    case Damping::Critical:
      return (c1_ + c2_ * time) * decay;
    case Damping::Under:
      return decay * (c1_ * std::cos(r2_ * time) + c2_ * std::sin(r2_ * time));
  }
  return 0.0f;
}

float SpringSimulation::dx(float time) const {
  const float decay = std::exp(r1_ * time);
  switch (damping_) {
    case Damping::Over:
      return c1_ * r1_ * decay + c2_ * r2_ * std::exp(r2_ * time);
    case Damping::Critical:
      return decay * (c2_ + r1_ * (c1_ + c2_ * time));
    case Damping::Under: {
      const float cosine = std::cos(r2_ * time);
      const float sine = std::sin(r2_ * time);
      return decay * (r2_ * (c2_ * cosine - c1_ * sine) + r1_ * (c1_ * cosine + c2_ * sine));
    }
  }
  return 0.0f;
}

// ---------------------------------------------------------------------------
// ClampingScrollSimulation
// ---------------------------------------------------------------------------

namespace {

/// Android's constants, kept in their original form so the curve is recognisably
/// the platform's rather than an approximation of it.
constexpr float kDecelerationRate = 2.3582017f;  // log(0.78) / log(0.9)
constexpr float kFrictionScale = 0.84f * 61774.04968f;

float flingDuration(float velocity, float friction) noexcept {
  const float scaled = friction * kFrictionScale;
  return std::exp(std::log(0.35f * std::fabs(velocity) / scaled) / (kDecelerationRate - 1.0f));
}

}  // namespace

ClampingScrollSimulation::ClampingScrollSimulation(float position, float velocity, float friction,
                                                   Tolerance settling) noexcept
    : Simulation(settling), position_(position), velocity_(velocity) {
  duration_ = std::fabs(velocity) < settling.velocity ? 0.0f : flingDuration(velocity, friction);
  distance_ = std::fabs(velocity) * duration_ / kDecelerationRate;
}

float ClampingScrollSimulation::x(float time) const {
  if (duration_ <= 0.0f) return position_;
  const float t = std::clamp(time / duration_, 0.0f, 1.0f);
  const float travelled = distance_ * (1.0f - std::pow(1.0f - t, kDecelerationRate));
  return position_ + std::copysign(travelled, velocity_);
}

float ClampingScrollSimulation::dx(float time) const {
  if (duration_ <= 0.0f) return 0.0f;
  const float t = std::clamp(time / duration_, 0.0f, 1.0f);
  const float speed =
      distance_ * kDecelerationRate * std::pow(1.0f - t, kDecelerationRate - 1.0f) / duration_;
  return std::copysign(speed, velocity_);
}

// ---------------------------------------------------------------------------
// BouncingScrollSimulation
// ---------------------------------------------------------------------------

namespace {

constexpr float kBouncingDrag = 0.135f;
/// However fast the friction was going when it reached the edge, the spring
/// never inherits more than this -- an unclamped transfer throws the content
/// most of a screen past the end before it turns around.
constexpr float kMaxSpringTransferVelocity = 5000.0f;

}  // namespace

BouncingScrollSimulation::BouncingScrollSimulation(float position, float velocity,
                                                   float leadingExtent, float trailingExtent,
                                                   SpringDescription spring,
                                                   Tolerance settling) noexcept
    : Simulation(settling) {
  if (position < leadingExtent) {
    spring_.emplace(spring, position, leadingExtent, velocity, settling);
    springTime_ = -kInf;
    return;
  }
  if (position > trailingExtent) {
    spring_.emplace(spring, position, trailingExtent, velocity, settling);
    springTime_ = -kInf;
    return;
  }

  friction_.emplace(kBouncingDrag, position, velocity, settling);
  const float rest = friction_->finalX();
  if (velocity > 0.0f && rest > trailingExtent) {
    springTime_ = friction_->timeAtX(trailingExtent);
    spring_.emplace(spring, trailingExtent, trailingExtent,
                    std::min(friction_->dx(springTime_), kMaxSpringTransferVelocity), settling);
  } else if (velocity < 0.0f && rest < leadingExtent) {
    springTime_ = friction_->timeAtX(leadingExtent);
    spring_.emplace(spring, leadingExtent, leadingExtent,
                    std::max(friction_->dx(springTime_), -kMaxSpringTransferVelocity), settling);
  }
}

const Simulation& BouncingScrollSimulation::phaseAt(float time, float& local) const {
  if (time > springTime_) {
    local = std::isfinite(springTime_) ? time - springTime_ : time;
    return *spring_;
  }
  local = time;
  return *friction_;
}

float BouncingScrollSimulation::x(float time) const {
  float local = 0.0f;
  return phaseAt(time, local).x(local);
}

float BouncingScrollSimulation::dx(float time) const {
  float local = 0.0f;
  return phaseAt(time, local).dx(local);
}

bool BouncingScrollSimulation::isDone(float time) const {
  float local = 0.0f;
  return phaseAt(time, local).isDone(local);
}

}  // namespace fltr
