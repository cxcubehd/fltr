#include "game/levels.hh"

namespace demo {

namespace {

// Static storage: `Text` holds a `string_view` into whatever it is given and
// does not copy, so every string the UI shows has to outlive the build. String
// literals with static storage duration are the simplest way to be sure.
constexpr LevelDef kLevels[] = {
    {"Shakedown", "Four slow rocks. Learn the drift.", 4, 34.0f, 0},
    {"Kuiper Shelf", "Six rocks, a little quicker.", 6, 52.0f, 200},
    {"Trojan Gap", "Eight rocks on crossing courses.", 8, 68.0f, 700},
    {"The Scatter", "Ten rocks, no room to coast.", 10, 88.0f, 1600},
    {"Perihelion", "Twelve rocks at speed.", 12, 112.0f, 3000},
    {"Oort Drift", "Fourteen rocks and a long fall.", 14, 128.0f, 4800},
    {"Roche Limit", "Sixteen rocks, tightly packed.", 16, 146.0f, 7000},
    {"Terminator", "Eighteen rocks. Nothing forgiving.", 18, 168.0f, 9500},
};

}  // namespace

std::span<const LevelDef> levels() noexcept { return kLevels; }

bool Progress::unlocked(std::size_t index) const noexcept {
  if (index >= std::size(kLevels)) return false;
  return best_ >= kLevels[index].unlockScore;
}

void Progress::recordScore(int score) noexcept {
  if (score > best_) best_ = score;
}

}  // namespace demo
