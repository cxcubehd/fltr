#pragma once

#include "fltr/core/observable.hpp"
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

  /// Escape was pressed and nothing in the UI took it. A shortcut only fires
  /// while the focus is inside the subtree that declares it, and a HUD has no
  /// reason to hold the focus at all -- so the app-wide meaning of Escape lives
  /// here, on the same "whatever the UI declined" footing as the ship's keys.
  bool backRequested() const noexcept { return backRequested_; }

  /// Set while a menu is up, so held movement keys stop reaching the ship even
  /// though raylib still reports them as down.
  void setGameHasFocus(bool value) noexcept { gameHasFocus_ = value; }

  /// Whether the player is currently driving with the keyboard. A key turns it
  /// on, the mouse turns it off, and the UI shows focus rings only while it is
  /// on -- so a fresh window is not covered in keyboard affordances nobody asked
  /// for. Focus itself is unaffected: this is what the focus *looks* like, which
  /// is the consumer's half of the bargain, not the framework's.
  fltr::Observable<bool>& keyboardMode() noexcept { return keyboardMode_; }

private:
  void pumpPointer(fltr::WidgetBinding& binding);
  void pumpSignals(fltr::WidgetBinding& binding);
  void pumpKeys(fltr::WidgetBinding& binding);

  fltr::Observable<bool> keyboardMode_{false};
  ShipInput ship_;
  fltr::Offset lastPointer_{-1.0f, -1.0f};
  bool pointerDown_ = false;
  bool gameHasFocus_ = false;
  bool backRequested_ = false;
};

/// Exposed for the walkthrough and for the headless smoke test, which
/// synthesises key events rather than reading a keyboard.
fltr::PhysicalKey physicalKeyFor(int raylibKey) noexcept;
fltr::LogicalKey logicalKeyFor(int raylibKey) noexcept;

}  // namespace demo
