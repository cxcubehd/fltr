#pragma once

#include <cstddef>
#include <span>

namespace demo {

/// One asteroid field. `unlockScore` is what the previous run has to have banked
/// before this becomes selectable, which is what gives the level list a disabled
/// state to publish.
struct LevelDef {
  const char* name;
  const char* blurb;
  int rocks;
  float driftSpeed;
  int unlockScore;
};

std::span<const LevelDef> levels() noexcept;

/// Which levels the player has unlocked. Plain state -- the UI reads it through
/// `Session`, which is what owns the observables.
class Progress {
public:
  bool unlocked(std::size_t index) const noexcept;
  void recordScore(int score) noexcept;
  int best() const noexcept { return best_; }

private:
  int best_ = 0;
};

}  // namespace demo
