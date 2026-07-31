#pragma once

#include <cstdint>

#include "fltr/animation/curves.hpp"
#include "fltr/animation/ticker.hpp"

namespace fltr {

/// Where an animation is in its lifecycle. `Dismissed` and `Completed` mean
/// settled at the bottom and the top of the range; settling anywhere in between
/// reports the end it was travelling toward.
enum class AnimationStatus : std::uint8_t { Dismissed, Forward, Reverse, Completed };

/// Drives a normalized 0..1 value from ticker time.
///
/// Value and status are notified independently -- a value listener runs every
/// tick, a status listener only at the transitions -- so logic that waits for an
/// animation to settle or to turn around is not woken by every frame of it.
///
/// `duration` is the time for the *full* range, so a shorter journey takes
/// proportionally less time. That is what makes interruption behave: reversing
/// from 0.3 takes 30% of the duration rather than all of it, and the pointer
/// entering and leaving repeatedly never drifts.
class AnimationDriver final : public Listenable {
public:
  struct Config {
    float duration = 0.2f;
    Curve curve;
    /// Unset means `curve` in both directions, which is the continuous choice: a
    /// distinct reverse curve changes the value at the instant of the reversal.
    Curve reverseCurve;
  };

  AnimationDriver() = default;
  explicit AnimationDriver(Config config) noexcept : config_(config) {}

  /// A driver ticks only once it has a registry. Attaching is what a State does
  /// from `initState`, and detaching is what it does from `dispose`.
  void attach(TickerRegistry& tickers) { ticker_.attach(tickers); }
  void detach() { ticker_.detach(); }

  void setConfig(Config config) noexcept { config_ = config; }
  const Config& config() const noexcept { return config_; }

  /// Muting stops time reaching this driver without disturbing where it is.
  void setMuted(bool muted) { ticker_.setMuted(muted); }
  bool muted() const noexcept { return ticker_.muted(); }

  /// Position in the range, before easing.
  float progress() const noexcept { return progress_; }
  /// Position after easing, which is what an interpolation is sampled at.
  float value() const noexcept;
  AnimationStatus status() const noexcept { return status_; }
  bool isAnimating() const noexcept { return ticker_.started(); }
  bool ticking() const noexcept { return ticker_.ticking(); }

  /// The separate channel: subscribe here to learn that an animation settled or
  /// turned around, without being woken by every frame of it.
  Listenable& statusChanges() noexcept { return statusChanges_; }

  void forward() { animateTo(1.0f); }
  void reverse() { animateTo(0.0f); }

  /// Retargets from wherever the animation currently is, rather than restarting.
  void animateTo(float target);

  /// Ambient motion. `pingPong` reflects at the ends instead of wrapping.
  void repeat(bool pingPong = false);

  void jumpTo(float progress);
  void stop();

private:
  void tick(float seconds);
  void setProgress(float progress);
  void setStatus(AnimationStatus status);
  void settle();

  Ticker ticker_{[](void* self, float seconds) {
                   static_cast<AnimationDriver*>(self)->tick(seconds);
                 },
                 this};
  Notifier statusChanges_;
  Config config_;
  float progress_ = 0.0f;
  float target_ = 0.0f;
  /// Position within one repeat cycle, which for ping-pong runs 0..2 and folds
  /// back into progress. Only meaningful while repeating.
  float phase_ = 0.0f;
  bool repeating_ = false;
  bool pingPong_ = false;
  AnimationStatus status_ = AnimationStatus::Dismissed;
};

}  // namespace fltr
