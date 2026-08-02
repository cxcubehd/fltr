#include "game/session.hh"

#include <cmath>
#include <cstdio>

namespace demo {

namespace {

/// snprintf into a fixed buffer and hand back a view of exactly what it wrote.
/// No allocation, and the storage belongs to the caller, which is what `Text`
/// requires of every string it is given.
std::string_view print(char* buffer, std::size_t capacity, int value) {
  const int written = std::snprintf(buffer, capacity, "%d", value);
  if (written <= 0) return {buffer, 0};
  return {buffer, static_cast<std::size_t>(written)};
}

}  // namespace

void Session::start(std::size_t level, fltr::Size bounds) {
  const std::span<const LevelDef> table = levels();
  level_ = level < table.size() ? level : 0;
  const LevelDef& def = table[level_];

  field_.reset(bounds, def.rocks, def.driftSpeed, static_cast<std::uint32_t>(level_ * 7919u + 13u));
  ship_.reset({bounds.width * 0.5f, bounds.height * 0.5f});
  scoreValue_ = 0;
  paused_ = false;
  outcome.set(Outcome::Flying);
  publish();
}

void Session::setBounds(fltr::Size bounds) { field_.setBounds(bounds); }

void Session::tick(const ShipInput& input, float dt) {
  if (paused_ || outcome.value() != Outcome::Flying) return;

  const int before = field_.destroyed();
  ship_.tick(input, dt, field_.bounds());
  const float damage = field_.tick(ship_, dt);
  if (damage > 0.0f) ship_.applyDamage(damage);

  scoreValue_ += (field_.destroyed() - before) * 25;

  if (!ship_.alive()) {
    progress_.recordScore(scoreValue_);
    outcome.set(Outcome::Destroyed);
  } else if (field_.cleared()) {
    progress_.recordScore(scoreValue_ + 500);
    scoreValue_ += 500;
    outcome.set(Outcome::Cleared);
  }
}

void Session::format() {
  scoreText_ = print(scoreBuf_, sizeof(scoreBuf_), scoreValue_);
  speedText_ = print(speedBuf_, sizeof(speedBuf_), speed.value());
  remainingText_ = print(remainingBuf_, sizeof(remainingBuf_), remaining.value());
  bestText_ = print(bestBuf_, sizeof(bestBuf_), progress_.best());
}

void Session::publish() {
  hullFraction.set(ship_.hull() / Ship::kHullMax);
  shieldFraction.set(ship_.shield() / Ship::kShieldMax);
  remaining.set(static_cast<int>(field_.asteroids().size()));
  score.set(scoreValue_);
  speed.set(static_cast<int>(std::lround(ship_.speed())));
  format();
}

std::string_view Session::levelName() const noexcept {
  const std::span<const LevelDef> table = levels();
  return level_ < table.size() ? std::string_view{table[level_].name} : std::string_view{};
}

}  // namespace demo
