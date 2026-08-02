#include "app/clock.hh"

#include <algorithm>

#include "raylib.h"

namespace demo {

float Clock::advance() {
  const float dt = fixedStep_ > 0.0f ? fixedStep_ : GetFrameTime();
  const float clamped = std::clamp(dt, 0.0f, 0.1f);
  elapsed_ += clamped;
  return clamped;
}

}  // namespace demo
