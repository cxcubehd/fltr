#pragma once

#include <cstddef>

#include "fltr/core/listenable.hpp"

namespace fltr {

/// The frame clock. Time comes from the consumer's game loop, so this holds the
/// current frame's elapsed seconds and every active ticker reads it from here.
///
/// It is a Listenable rather than a list of tickers, which buys two things
/// already built and tested: starting or stopping a ticker from inside a tick is
/// safe, and a ticker that is not running holds no subscription -- so "an
/// animation in a hidden panel costs nothing" is a count anyone can check rather
/// than a claim.
class TickerRegistry {
public:
  /// One frame's elapsed time. Variable by nature: the consumer's loop is not
  /// vsync-driven and may run at any rate.
  void tick(float seconds) {
    FLTR_EXPECTS(seconds >= 0.0f, "a frame's elapsed time may not be negative");
    delta_ = seconds;
    frames_.notifyListeners();
  }

  float delta() const noexcept { return delta_; }
  bool hasActiveTickers() const noexcept { return frames_.hasListeners(); }
  std::size_t activeTickerCount() const noexcept { return frames_.listenerCount(); }

private:
  friend class Ticker;

  Notifier frames_;
  float delta_ = 0.0f;
};

/// Elapsed time per frame, delivered to whoever created it.
///
/// A ticker is owned by the object that created it -- a State, through the
/// driver it holds -- and receives time only while it is started, unmuted, and
/// attached to a registry. Anything else and it is not subscribed at all.
///
/// DIVERGENCE: time arrives as a per-frame delta rather than as an absolute
/// clock. That is what makes muting exact: a muted ticker is simply not
/// subscribed, so it resumes where it stopped with no elapsed time to reconcile
/// and no start offset to carry.
class Ticker {
public:
  using Fn = void (*)(void* ctx, float seconds);

  Ticker(Fn fn, void* ctx) noexcept : fn_(fn), ctx_(ctx) {
    FLTR_EXPECTS(fn != nullptr, "a ticker needs something to tick");
  }

  Ticker(const Ticker&) = delete;
  Ticker& operator=(const Ticker&) = delete;

  /// The registry this ticker draws time from. Until it has one it behaves as
  /// muted, so a driver started before its element reached the tree simply
  /// begins when it gets there.
  void attach(TickerRegistry& registry) {
    if (&registry == registry_) return;
    registry_ = &registry;
    subscription_.detach();
    sync();
  }

  void detach() {
    registry_ = nullptr;
    sync();
  }

  void start() {
    started_ = true;
    sync();
  }

  void stop() {
    started_ = false;
    sync();
  }

  void setMuted(bool muted) {
    if (muted == muted_) return;
    muted_ = muted;
    sync();
  }

  bool started() const noexcept { return started_; }
  bool muted() const noexcept { return muted_; }
  /// Subscribed and receiving time.
  bool ticking() const noexcept { return subscription_.attached(); }

private:
  void sync() {
    const bool shouldTick = started_ && !muted_ && registry_ != nullptr;
    if (shouldTick == subscription_.attached()) return;
    if (!shouldTick) {
      subscription_.detach();
      return;
    }
    registry_->frames_.subscribe(
        subscription_,
        [](void* p) {
          Ticker& self = *static_cast<Ticker*>(p);
          self.fn_(self.ctx_, self.registry_->delta());
        },
        this);
  }

  TickerRegistry* registry_ = nullptr;
  Subscription subscription_;
  Fn fn_;
  void* ctx_;
  bool started_ = false;
  bool muted_ = false;
};

}  // namespace fltr
