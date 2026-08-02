#pragma once

#include "fltr/core/geometry.hpp"

namespace demo {

/// What the player is holding down this frame. Filled by the input layer from
/// keys the UI declined, so a menu that has the keyboard silently stops the ship
/// rather than fighting it.
struct ShipInput {
  bool thrust = false;
  bool left = false;
  bool right = false;
  bool fire = false;
};

/// A floaty ship: thrust accelerates along the heading, nothing decelerates it
/// but a small drag, and the velocity carries. Turning does not change where you
/// are going, which is the whole feel.
class Ship {
public:
  static constexpr float kHullMax = 100.0f;
  static constexpr float kShieldMax = 50.0f;
  static constexpr float kRadius = 14.0f;

  void reset(fltr::Offset at);
  void tick(const ShipInput& input, float dt, fltr::Size bounds);

  /// Consumes the queued shot, if the cooldown allows one.
  bool takeShot(fltr::Offset& muzzle, fltr::Offset& velocity);

  /// Shield absorbs first; whatever is left comes off the hull.
  void applyDamage(float amount);

  fltr::Offset position() const noexcept { return position_; }
  fltr::Offset velocity() const noexcept { return velocity_; }
  float heading() const noexcept { return heading_; }
  float speed() const noexcept { return velocity_.distance(); }
  float hull() const noexcept { return hull_; }
  float shield() const noexcept { return shield_; }
  bool thrusting() const noexcept { return thrusting_; }
  bool alive() const noexcept { return hull_ > 0.0f; }

private:
  fltr::Offset position_;
  fltr::Offset velocity_;
  float heading_ = -1.5707963f;  // pointing up
  float hull_ = kHullMax;
  float shield_ = kShieldMax;
  float cooldown_ = 0.0f;
  bool thrusting_ = false;
  bool wantsShot_ = false;
};

}  // namespace demo
