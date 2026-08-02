#pragma once

#include "fltr/gestures/events.hpp"
#include "fltr/gestures/keys.hpp"
#include "fltr/widgets/binding.hpp"
#include "game/ship.hh"

namespace demo {

/// Turns raylib's polled input into the events fltr dispatches, and keeps what
/// the UI declined for the game.
///
/// Dispatch is deliberately unrelated to the frame: an event goes in when it
/// arrives, and `drawFrame` is a separate call. `dispatchKey` and
/// `dispatchSignal` both answer "did the UI take this", which is the only thing
/// the game needs to know to avoid fighting a menu for the arrow keys.
class Input {
public:
  /// Pushes everything raylib saw since the last call.
  void pump(fltr::WidgetBinding& binding);

  /// What the ship should do this frame: whatever the UI did not consume.
  const ShipInput& ship() const noexcept { return ship_; }

  /// Set while a menu is up, so held movement keys stop reaching the ship even
  /// though raylib still reports them as down.
  void setGameHasFocus(bool value) noexcept { gameHasFocus_ = value; }

private:
  void pumpPointer(fltr::WidgetBinding& binding);
  void pumpSignals(fltr::WidgetBinding& binding);
  void pumpKeys(fltr::WidgetBinding& binding);

  ShipInput ship_;
  fltr::Offset lastPointer_{-1.0f, -1.0f};
  bool pointerDown_ = false;
  bool gameHasFocus_ = false;
};

/// Exposed for the walkthrough and for the headless smoke test, which
/// synthesises key events rather than reading a keyboard.
fltr::PhysicalKey physicalKeyFor(int raylibKey) noexcept;
fltr::LogicalKey logicalKeyFor(int raylibKey) noexcept;

}  // namespace demo
