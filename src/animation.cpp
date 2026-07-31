#include "fltr/animation/driver.hpp"

#include <algorithm>
#include <cmath>

namespace fltr {
namespace {

constexpr float clampToRange(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

float AnimationDriver::value() const noexcept {
  const bool backwards = status_ == AnimationStatus::Reverse && config_.reverseCurve;
  return backwards ? config_.reverseCurve(progress_) : config_.curve(progress_);
}

void AnimationDriver::animateTo(float target) {
  repeating_ = false;
  target_ = clampToRange(target);
  if (target_ == progress_) {
    settle();
    return;
  }
  setStatus(target_ > progress_ ? AnimationStatus::Forward : AnimationStatus::Reverse);
  ticker_.start();
}

void AnimationDriver::repeat(bool pingPong) {
  repeating_ = true;
  pingPong_ = pingPong;
  phase_ = progress_;
  setStatus(AnimationStatus::Forward);
  ticker_.start();
}

void AnimationDriver::jumpTo(float progress) {
  repeating_ = false;
  target_ = clampToRange(progress);
  setProgress(target_);
  settle();
}

void AnimationDriver::stop() {
  repeating_ = false;
  target_ = progress_;
  settle();
}

void AnimationDriver::tick(float seconds) {
  // A step is a fraction of the whole range, so the same duration governs a full
  // journey and a partial one. A duration of zero arrives in one tick.
  const float step = config_.duration > 0.0f ? seconds / config_.duration : 1.0f;

  if (repeating_) {
    phase_ = std::fmod(phase_ + step, pingPong_ ? 2.0f : 1.0f);
    const bool descending = pingPong_ && phase_ > 1.0f;
    setStatus(descending ? AnimationStatus::Reverse : AnimationStatus::Forward);
    setProgress(descending ? 2.0f - phase_ : phase_);
    return;
  }

  const bool forwards = target_ > progress_;
  const float next = forwards ? progress_ + step : progress_ - step;
  if (forwards ? next >= target_ : next <= target_) {
    setProgress(target_);
    settle();
    return;
  }
  setProgress(next);
}

void AnimationDriver::setProgress(float progress) {
  if (progress == progress_) return;
  progress_ = progress;
  notifyListeners();
}

void AnimationDriver::setStatus(AnimationStatus status) {
  if (status == status_) return;
  status_ = status;
  statusChanges_.notifyListeners();
}

void AnimationDriver::settle() {
  ticker_.stop();
  if (progress_ <= 0.0f) {
    setStatus(AnimationStatus::Dismissed);
  } else if (progress_ >= 1.0f) {
    setStatus(AnimationStatus::Completed);
  } else if (status_ == AnimationStatus::Forward) {
    setStatus(AnimationStatus::Completed);
  } else if (status_ == AnimationStatus::Reverse) {
    setStatus(AnimationStatus::Dismissed);
  }
}

}  // namespace fltr
