#include "game/field.hh"

#include <cmath>

namespace demo {

using fltr::Offset;
using fltr::Size;

namespace {

constexpr float kTau = 6.2831853f;

float radiusForTier(int tier) {
  switch (tier) {
    case 3: return 44.0f;
    case 2: return 26.0f;
    default: return 14.0f;
  }
}

float wrap(float value, float extent) {
  if (extent <= 0.0f) return value;
  while (value < 0.0f) value += extent;
  while (value >= extent) value -= extent;
  return value;
}

bool overlaps(Offset a, float ra, Offset b, float rb) {
  const float reach = ra + rb;
  return (a - b).distanceSquared() <= reach * reach;
}

}  // namespace

float Field::randomUnit() {
  // xorshift32: deterministic, seedable, and no <random> in a per-frame path.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return static_cast<float>(rng_ % 100000u) / 100000.0f;
}

void Field::reset(Size bounds, int rocks, float speed, std::uint32_t seed) {
  bounds_ = bounds;
  rng_ = seed == 0u ? 1u : seed;
  destroyed_ = 0;
  bullets_.clear();
  asteroids_.clear();
  asteroids_.reserve(static_cast<std::size_t>(rocks) * 4u);

  const Offset centre{bounds.width * 0.5f, bounds.height * 0.5f};
  for (int i = 0; i < rocks; ++i) {
    Asteroid rock;
    rock.tier = 3;
    rock.radius = radiusForTier(rock.tier);

    // Spawned away from the middle, so the ship is not hit on frame one.
    do {
      rock.position = {randomUnit() * bounds.width, randomUnit() * bounds.height};
    } while ((rock.position - centre).distance() < 180.0f);

    const float heading = randomUnit() * kTau;
    rock.velocity = Offset{std::cos(heading), std::sin(heading)} * (speed * (0.6f + randomUnit()));
    rock.angle = randomUnit() * kTau;
    rock.spin = (randomUnit() - 0.5f) * 1.4f;
    asteroids_.push_back(rock);
  }
}

void Field::split(std::size_t index) {
  const Asteroid parent = asteroids_[index];
  ++destroyed_;

  // Remove by swapping with the back: order carries no meaning here, and this
  // keeps the per-frame path free of shifting a vector.
  asteroids_[index] = asteroids_.back();
  asteroids_.pop_back();

  if (parent.tier <= 1) return;

  for (int i = 0; i < 2; ++i) {
    Asteroid child;
    child.tier = parent.tier - 1;
    child.radius = radiusForTier(child.tier);
    child.position = parent.position;
    const float heading = randomUnit() * kTau;
    const float speed = parent.velocity.distance() * (1.15f + randomUnit() * 0.4f);
    child.velocity = Offset{std::cos(heading), std::sin(heading)} * std::max(speed, 30.0f);
    child.angle = randomUnit() * kTau;
    child.spin = (randomUnit() - 0.5f) * 2.2f;
    asteroids_.push_back(child);
  }
}

float Field::tick(Ship& ship, float dt) {
  Offset muzzle;
  Offset shotVelocity;
  if (ship.takeShot(muzzle, shotVelocity)) {
    bullets_.push_back(Bullet{.position = muzzle, .velocity = shotVelocity});
  }

  for (std::size_t i = bullets_.size(); i-- > 0;) {
    Bullet& bullet = bullets_[i];
    bullet.position += bullet.velocity * dt;
    bullet.position = {wrap(bullet.position.dx, bounds_.width),
                       wrap(bullet.position.dy, bounds_.height)};
    bullet.life -= dt;
    if (bullet.life <= 0.0f) {
      bullets_[i] = bullets_.back();
      bullets_.pop_back();
    }
  }

  for (Asteroid& rock : asteroids_) {
    rock.position += rock.velocity * dt;
    rock.position = {wrap(rock.position.dx, bounds_.width), wrap(rock.position.dy, bounds_.height)};
    rock.angle += rock.spin * dt;
  }

  // Bullets against rocks. Both lists are walked backwards so a swap-and-pop
  // never invalidates an index still to be visited.
  for (std::size_t b = bullets_.size(); b-- > 0;) {
    for (std::size_t a = asteroids_.size(); a-- > 0;) {
      if (!overlaps(bullets_[b].position, 2.0f, asteroids_[a].position, asteroids_[a].radius)) {
        continue;
      }
      bullets_[b] = bullets_.back();
      bullets_.pop_back();
      split(a);
      break;
    }
  }

  float damage = 0.0f;
  if (ship.alive()) {
    for (std::size_t a = asteroids_.size(); a-- > 0;) {
      if (!overlaps(ship.position(), Ship::kRadius, asteroids_[a].position, asteroids_[a].radius)) {
        continue;
      }
      damage += 8.0f + 6.0f * static_cast<float>(asteroids_[a].tier);
      split(a);
    }
  }
  return damage;
}

}  // namespace demo
