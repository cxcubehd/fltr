#include "platform/input.hh"

#include "raylib.h"

namespace demo {

using fltr::KeyEvent;
using fltr::KeyEventType;
using fltr::KeyModifier;
using fltr::KeyModifiers;
using fltr::LogicalKey;
using fltr::Offset;
using fltr::PhysicalKey;
using fltr::PointerDeviceKind;
using fltr::PointerEvent;
using fltr::PointerPhase;
using fltr::PointerSignalEvent;
using fltr::PointerSignalKind;

namespace {

/// Only the keys this demo binds. A real port would table-drive the whole
/// keyboard; the point here is that the mapping belongs to the consumer,
/// because only the consumer knows what its input system calls things.
struct KeyPair {
  int raylib;
  PhysicalKey physical;
  LogicalKey logical;
  /// Flown with. A key the ship uses is withheld from the UI while the ship has
  /// the controls, so thrusting or firing cannot also activate whatever the
  /// focus happens to be sitting on.
  bool flies = false;
};

constexpr KeyPair kKeys[] = {
    {KEY_ESCAPE, PhysicalKey::Escape, LogicalKey::Escape},
    {KEY_ENTER, PhysicalKey::Enter, LogicalKey::Enter},
    {KEY_KP_ENTER, PhysicalKey::NumpadEnter, LogicalKey::Enter},
    {KEY_TAB, PhysicalKey::Tab, LogicalKey::Tab},
    {KEY_SPACE, PhysicalKey::Space, LogicalKey::Space, true},
    {KEY_BACKSPACE, PhysicalKey::Backspace, LogicalKey::Backspace},
    {KEY_LEFT, PhysicalKey::ArrowLeft, LogicalKey::ArrowLeft, true},
    {KEY_RIGHT, PhysicalKey::ArrowRight, LogicalKey::ArrowRight, true},
    {KEY_UP, PhysicalKey::ArrowUp, LogicalKey::ArrowUp, true},
    {KEY_DOWN, PhysicalKey::ArrowDown, LogicalKey::ArrowDown, true},
    {KEY_HOME, PhysicalKey::Home, LogicalKey::Home},
    {KEY_END, PhysicalKey::End, LogicalKey::End},
    {KEY_PAGE_UP, PhysicalKey::PageUp, LogicalKey::PageUp},
    {KEY_PAGE_DOWN, PhysicalKey::PageDown, LogicalKey::PageDown},
    {KEY_W, PhysicalKey::KeyW, LogicalKey::KeyW, true},
    {KEY_A, PhysicalKey::KeyA, LogicalKey::KeyA, true},
    {KEY_S, PhysicalKey::KeyS, LogicalKey::KeyS},
    {KEY_D, PhysicalKey::KeyD, LogicalKey::KeyD, true},
    {KEY_P, PhysicalKey::KeyP, LogicalKey::KeyP},
    {KEY_Q, PhysicalKey::KeyQ, LogicalKey::KeyQ},
};

KeyModifiers currentModifiers() {
  KeyModifiers modifiers;
  if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
    modifiers |= KeyModifiers{KeyModifier::Shift};
  }
  if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
    modifiers |= KeyModifiers{KeyModifier::Control};
  }
  if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
    modifiers |= KeyModifiers{KeyModifier::Alt};
  }
  return modifiers;
}

}  // namespace

PhysicalKey physicalKeyFor(int raylibKey) noexcept {
  for (const KeyPair& pair : kKeys) {
    if (pair.raylib == raylibKey) return pair.physical;
  }
  return PhysicalKey::None;
}

LogicalKey logicalKeyFor(int raylibKey) noexcept {
  for (const KeyPair& pair : kKeys) {
    if (pair.raylib == raylibKey) return pair.logical;
  }
  return LogicalKey::None;
}

void Input::pumpPointer(fltr::WidgetBinding& binding) {
  const Vector2 mouse = GetMousePosition();
  const Offset position{mouse.x, mouse.y};
  const bool moved = position != lastPointer_;

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    pointerDown_ = true;
    binding.dispatchPointer(PointerEvent{.phase = PointerPhase::Down,
                                         .pointer = 1,
                                         .kind = PointerDeviceKind::Mouse,
                                         .position = position,
                                         .modifiers = currentModifiers()});
  } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    pointerDown_ = false;
    binding.dispatchPointer(PointerEvent{.phase = PointerPhase::Up,
                                         .pointer = 1,
                                         .kind = PointerDeviceKind::Mouse,
                                         .position = position,
                                         .modifiers = currentModifiers()});
  } else if (moved) {
    // Hover and Move are different events, not the same one with a flag: hover
    // drives enter/exit, and a move during a press is routed to whatever
    // recognizer won the arena.
    binding.dispatchPointer(
        PointerEvent{.phase = pointerDown_ ? PointerPhase::Move : PointerPhase::Hover,
                     .pointer = 1,
                     .kind = PointerDeviceKind::Mouse,
                     .position = position,
                     .modifiers = currentModifiers()});
  }

  lastPointer_ = position;
}

void Input::pumpSignals(fltr::WidgetBinding& binding) {
  const float wheel = GetMouseWheelMove();
  if (wheel == 0.0f) return;

  const Vector2 mouse = GetMousePosition();
  // The notch size belongs to the platform, so the conversion to logical pixels
  // happens here rather than inside the framework.
  binding.dispatchSignal(PointerSignalEvent{.kind = PointerSignalKind::Scroll,
                                            .pointer = 1,
                                            .position = {mouse.x, mouse.y},
                                            .localPosition = {mouse.x, mouse.y},
                                            .delta = {0.0f, -wheel * 48.0f},
                                            .modifiers = currentModifiers()});
}

void Input::pumpKeys(fltr::WidgetBinding& binding) {
  const KeyModifiers modifiers = currentModifiers();
  backRequested_ = false;

  for (const KeyPair& pair : kKeys) {
    if (pair.flies && gameHasFocus_) continue;
    if (IsKeyPressed(pair.raylib)) {
      const bool taken = binding.dispatchKey(KeyEvent{.type = KeyEventType::Down,
                                                      .physical = pair.physical,
                                                      .logical = pair.logical,
                                                      .modifiers = modifiers});
      if (!taken && pair.logical == LogicalKey::Escape) backRequested_ = true;
    }
    if (IsKeyReleased(pair.raylib)) {
      binding.dispatchKey(KeyEvent{.type = KeyEventType::Up,
                                   .physical = pair.physical,
                                   .logical = pair.logical,
                                   .modifiers = modifiers});
    }
  }
}

void Input::pump(fltr::WidgetBinding& binding) {
  pumpPointer(binding);
  pumpSignals(binding);
  pumpKeys(binding);

  // Held keys are read straight from raylib rather than accumulated from the
  // events above: a game wants the current state of the stick, not the history
  // of it. `gameHasFocus_` is what a menu uses to take the ship's hands off the
  // controls without either side knowing about the other.
  ship_ = ShipInput{};
  if (!gameHasFocus_) return;
  ship_.thrust = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
  ship_.left = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
  ship_.right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
  ship_.fire = IsKeyDown(KEY_SPACE);
}

}  // namespace demo
