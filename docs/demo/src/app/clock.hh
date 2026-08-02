#pragma once

namespace demo {

/// Where the frame's elapsed time comes from.
///
/// fltr owns no clock: `drawFrame(seconds)` takes a delta the consumer
/// measured. That is also what makes the smoke test possible -- a fixed step
/// here drives animations deterministically, with no real time involved.
class Clock {
public:
  explicit Clock(float fixedStep = 0.0f) noexcept : fixedStep_(fixedStep) {}

  /// Seconds since the previous call, clamped so a breakpoint or a stalled
  /// frame does not teleport the ship across the field.
  float advance();

  float elapsed() const noexcept { return elapsed_; }

private:
  float fixedStep_ = 0.0f;
  float elapsed_ = 0.0f;
};

}  // namespace demo
