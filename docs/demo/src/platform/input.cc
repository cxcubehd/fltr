#include "platform/input.hh"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

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

#ifdef __EMSCRIPTEN__

/// What the browser measured since the last frame took it, in CSS pixels.
///
/// raylib reads the same events through emscripten's GLFW shim, which quantizes
/// every one of them to at least a whole wheel notch and passes the horizontal
/// delta through beside it in raw pixels. A trackpad reports a few pixels at a
/// time, so through that shim a gentle swipe becomes a notch a frame -- in
/// whichever axis happened to be the larger number.
Offset pendingWheel;

float wheelPixels(double delta, unsigned int mode) {
  // Only Firefox reports lines, and page mode is rare enough that a screenful of
  // the browser's own guess is close enough.
  switch (mode) {
    case DOM_DELTA_LINE: return static_cast<float>(delta) * 16.0f;
    case DOM_DELTA_PAGE: return static_cast<float>(delta) * 800.0f;
    default: return static_cast<float>(delta);
  }
}

EM_BOOL accumulateWheel(int, const EmscriptenWheelEvent* event, void*) {
  pendingWheel.dx += wheelPixels(event->deltaX, event->deltaMode);
  pendingWheel.dy += wheelPixels(event->deltaY, event->deltaMode);
  return EM_TRUE;
}

/// A browser measures in CSS pixels and the surface is drawn in the device's, so
/// the same ratio the interface is scaled by converts one to the other. The sign
/// needs no flipping: the DOM and fltr both count downward scrolling as positive.
Offset takeWheelDelta() {
  const float ratio = static_cast<float>(emscripten_get_device_pixel_ratio());
  const Offset wheel{pendingWheel.dx * ratio, pendingWheel.dy * ratio};
  pendingWheel = {};
  return wheel;
}

#else

/// One notch, in the logical pixels the surface is measured in. Notches are all a
/// desktop reports -- a trackpad's gesture has been resolved into them, fractions
/// included, long before raylib sees it.
constexpr float kWheelNotch = 48.0f;

Offset takeWheelDelta() {
  // Per axis, rather than `GetMouseWheelMove`, which answers with whichever of
  // the two is larger and so lets a sideways drift steer a vertical list.
  const Vector2 wheel = GetMouseWheelMoveV();
  return {-wheel.x * kWheelNotch, -wheel.y * kWheelNotch};
}

#endif

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

Input::Input() {
#ifdef __EMSCRIPTEN__
  emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, accumulateWheel);
#endif
}

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

  if (moved || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) keyboardMode_.set(false);

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
  // The conversion to the pixels the surface is measured in belongs to the
  // platform, so it happens here rather than inside the framework.
  const Offset wheel = takeWheelDelta();
  if (wheel == Offset{}) return;

  const Vector2 mouse = GetMousePosition();
  binding.dispatchSignal(PointerSignalEvent{.kind = PointerSignalKind::Scroll,
                                            .pointer = 1,
                                            .position = {mouse.x, mouse.y},
                                            .localPosition = {mouse.x, mouse.y},
                                            .delta = wheel,
                                            .modifiers = currentModifiers()});
}

void Input::pumpKeys(fltr::WidgetBinding& binding) {
  const KeyModifiers modifiers = currentModifiers();
  backRequested_ = false;

  for (const KeyPair& pair : kKeys) {
    if (pair.flies && gameHasFocus_) continue;
    if (IsKeyPressed(pair.raylib)) {
      // Tab is what asks for the keyboard, and the only key that does. Escape,
      // the arrows and an activator all mean something on their own, and none of
      // them is a request to start walking the focus.
      if (pair.logical == LogicalKey::Tab) keyboardMode_.set(true);
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
