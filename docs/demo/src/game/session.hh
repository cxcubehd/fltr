#pragma once

#include <cstddef>
#include <string_view>

#include "fltr/core/observable.hpp"
#include "game/field.hh"
#include "game/levels.hh"
#include "game/ship.hh"

namespace demo {

enum class Outcome : std::uint8_t { Flying, Cleared, Destroyed };

/// The seam between the game and the UI.
///
/// The game writes plain members; `publish()` pushes them into observables once
/// per frame. Pushing an unchanged value is a no-op in fltr, so a frame in which
/// the score did not move rebuilds nothing that reads the score -- which is why
/// the readouts are separate observables rather than one snapshot struct.
class Session {
public:
  void start(std::size_t level, fltr::Size bounds);
  void setBounds(fltr::Size bounds);

  /// One step of simulation. Does nothing while paused or once the run is over,
  /// so the caller never has to guard the call.
  void tick(const ShipInput& input, float dt);
  void publish();

  void setPaused(bool paused) noexcept { paused_ = paused; }
  bool paused() const noexcept { return paused_; }

  const Ship& ship() const noexcept { return ship_; }
  const Field& field() const noexcept { return field_; }
  Progress& progress() noexcept { return progress_; }
  const Progress& progress() const noexcept { return progress_; }
  std::size_t level() const noexcept { return level_; }

  // ---- What the UI names ------------------------------------------------
  // Public because the widgets refer to them by pointer, which is fltr's rule
  // for everything a widget does not own.

  fltr::Observable<float> hullFraction{1.0f};
  fltr::Observable<float> shieldFraction{1.0f};
  fltr::Observable<int> remaining{0};
  fltr::Observable<int> score{0};
  /// Rounded to whole units on purpose: the ship's speed changes every frame,
  /// and an observable that only moves when the displayed digits move is the
  /// difference between rebuilding a readout 60 times a second and rebuilding it
  /// when it has something new to say.
  fltr::Observable<int> speed{0};
  fltr::Observable<Outcome> outcome{Outcome::Flying};

  // ---- Text the widgets point at ----------------------------------------
  // `Text` does not copy: these buffers are the storage its `string_view`s name,
  // and they outlive every build because this object does.

  std::string_view scoreText() const noexcept { return scoreText_; }
  std::string_view speedText() const noexcept { return speedText_; }
  std::string_view remainingText() const noexcept { return remainingText_; }
  std::string_view bestText() const noexcept { return bestText_; }
  std::string_view levelName() const noexcept;

private:
  void format();

  Ship ship_;
  Field field_;
  Progress progress_;
  std::size_t level_ = 0;
  int scoreValue_ = 0;
  bool paused_ = false;

  char scoreBuf_[16] = "0";
  char speedBuf_[16] = "0";
  char remainingBuf_[16] = "0";
  char bestBuf_[16] = "0";
  std::string_view scoreText_{scoreBuf_, 1};
  std::string_view speedText_{speedBuf_, 1};
  std::string_view remainingText_{remainingBuf_, 1};
  std::string_view bestText_{bestBuf_, 1};
};

}  // namespace demo
