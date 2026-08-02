#pragma once

#include <cstdint>
#include <vector>

#include "fltr/core/geometry.hpp"
#include "game/ship.hh"

namespace demo {

struct Asteroid {
  fltr::Offset position;
  fltr::Offset velocity;
  float radius = 40.0f;
  float angle = 0.0f;
  float spin = 0.0f;
  /// 3 is the largest; a hit splits it into two of the tier below, and tier 1
  /// simply dies. This is what makes "asteroids remaining" climb before it falls.
  int tier = 3;
};

struct Bullet {
  fltr::Offset position;
  fltr::Offset velocity;
  float life = 1.1f;
};

/// The asteroid field: the rocks, the shots, and the collisions between them.
/// It knows nothing about the UI, and the UI reads it only through `Session`.
class Field {
public:
  void reset(fltr::Size bounds, int rocks, float speed, std::uint32_t seed);

  /// Returns the damage the ship took this step, so the caller decides what a
  /// collision means rather than the field reaching into the ship.
  float tick(Ship& ship, float dt);

  void setBounds(fltr::Size bounds) noexcept { bounds_ = bounds; }
  fltr::Size bounds() const noexcept { return bounds_; }

  const std::vector<Asteroid>& asteroids() const noexcept { return asteroids_; }
  const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
  int destroyed() const noexcept { return destroyed_; }
  bool cleared() const noexcept { return asteroids_.empty(); }

private:
  float randomUnit();
  void split(std::size_t index);

  std::vector<Asteroid> asteroids_;
  std::vector<Bullet> bullets_;
  fltr::Size bounds_{1280.0f, 720.0f};
  std::uint32_t rng_ = 1u;
  int destroyed_ = 0;
};

}  // namespace demo
