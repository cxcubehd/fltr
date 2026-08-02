#include "game/ship.hh"

#include <cmath>

namespace demo {

using fltr::Offset;
using fltr::Size;

namespace {

constexpr float kThrust = 340.0f;
constexpr float kTurnRate = 3.4f;
constexpr float kDrag = 0.32f;
constexpr float kMaxSpeed = 520.0f;
constexpr float kFireInterval = 0.16f;
constexpr float kBulletSpeed = 620.0f;
constexpr float kShieldRegen = 3.5f;

/// The field wraps, so nothing ever leaves it. Modulo rather than clamp, so a
/// ship at full speed keeps its velocity across the seam.
float wrap(float value, float extent) {
  if (extent <= 0.0f) return value;
  while (value < 0.0f) value += extent;
  while (value >= extent) value -= extent;
  return value;
}

}  // namespace

void Ship::reset(Offset at) {
  position_ = at;
  velocity_ = Offset::zero();
  heading_ = -1.5707963f;
  hull_ = kHullMax;
  shield_ = kShieldMax;
  cooldown_ = 0.0f;
  thrusting_ = false;
  wantsShot_ = false;
}

void Ship::tick(const ShipInput& input, float dt, Size bounds) {
  if (input.left) heading_ -= kTurnRate * dt;
  if (input.right) heading_ += kTurnRate * dt;

  thrusting_ = input.thrust;
  if (thrusting_) {
    velocity_ += Offset{std::cos(heading_), std::sin(heading_)} * (kThrust * dt);
  }

  // Drag is exponential rather than linear so the ship never quite stops, which
  // is what makes the readouts move continuously and the HUD worth watching.
  velocity_ = velocity_ * std::exp(-kDrag * dt);
  if (const float speed = velocity_.distance(); speed > kMaxSpeed) {
    velocity_ = velocity_ * (kMaxSpeed / speed);
  }

  position_ += velocity_ * dt;
  position_ = {wrap(position_.dx, bounds.width), wrap(position_.dy, bounds.height)};

  cooldown_ = cooldown_ > dt ? cooldown_ - dt : 0.0f;
  if (input.fire) wantsShot_ = true;

  if (shield_ < kShieldMax && hull_ > 0.0f) {
    shield_ = std::min(kShieldMax, shield_ + kShieldRegen * dt);
  }
}

bool Ship::takeShot(Offset& muzzle, Offset& velocity) {
  if (!wantsShot_ || cooldown_ > 0.0f || !alive()) {
    wantsShot_ = false;
    return false;
  }
  wantsShot_ = false;
  cooldown_ = kFireInterval;

  const Offset forward{std::cos(heading_), std::sin(heading_)};
  muzzle = position_ + forward * (kRadius + 4.0f);
  velocity = velocity_ + forward * kBulletSpeed;
  return true;
}

void Ship::applyDamage(float amount) {
  const float absorbed = std::min(shield_, amount);
  shield_ -= absorbed;
  hull_ = std::max(0.0f, hull_ - (amount - absorbed));
}

}  // namespace demo
