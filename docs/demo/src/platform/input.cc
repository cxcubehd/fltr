#include "platform/input.hh"

#include <vector>

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

/// A press or a release, and where the pointer was when it happened. The
/// position travels with the event because a click is over long before the frame
/// that reports it, and by then the pointer may be somewhere else.
struct ButtonEvent {
  PointerPhase phase;
  Offset position;
};

/// Everything the left button did since the last frame, oldest first.
std::vector<ButtonEvent> buttons;

#ifdef __EMSCRIPTEN__

/// Where the browser last put the pointer, and whether it is still over the
/// page. One source for the position, so a hover, a press and a wheel notch
/// cannot disagree about what is under the cursor.
Offset trackedPointer;
bool pointerLeft = false;

/// `targetX` is measured in the page's pixels, from the corner of the canvas.
/// The surface is measured in the display's, so it is the same conversion the
/// wheel needs.
Offset devicePixels(const EmscriptenMouseEvent& event) {
  const float ratio = static_cast<float>(emscripten_get_device_pixel_ratio());
  return {static_cast<float>(event.targetX) * ratio, static_cast<float>(event.targetY) * ratio};
}

EM_BOOL trackPointer(int type, const EmscriptenMouseEvent* event, void*) {
  pointerLeft = type == EMSCRIPTEN_EVENT_MOUSELEAVE;
  if (!pointerLeft) trackedPointer = devicePixels(*event);
  return EM_FALSE;
}

/// Presses and releases are taken from the browser rather than from raylib.
///
/// raylib samples the button once a frame and reports the edge it finds between
/// two samples, so a press and a release that both land inside one frame cancel
/// out: the click is never seen at all. A frame is however long the last one
/// took, and a click -- a trackpad tap especially -- is over in a few tens of
/// milliseconds, so that is not a rare case but a common one.
EM_BOOL accumulateButton(int type, const EmscriptenMouseEvent* event, void*) {
  // The left button is the only one the interface uses; the rest belong to the
  // page, which is why nothing here reports the event as consumed.
  if (event->button != 0) return EM_FALSE;

  trackedPointer = devicePixels(*event);
  pointerLeft = false;
  buttons.push_back(
      {.phase = type == EMSCRIPTEN_EVENT_MOUSEDOWN ? PointerPhase::Down : PointerPhase::Up,
       .position = trackedPointer});
  return EM_FALSE;
}

/// The browser filled the queue as the events arrived.
void collectButtons(Offset) {}

/// Where the pointer is, in the pixels the surface is measured in.
///
/// raylib's own answer goes through emscripten's GLFW shim, which scales it by
/// the ratio between the window size it was last told about and the canvas box
/// -- a second opinion about a number the page already knows exactly, and one
/// that disagrees for a frame whenever the canvas is resized. A pointer that
/// left the page is hovering nothing, which nothing else will say either; one
/// dragging something keeps its position, because letting go is the gesture's
/// business rather than the cursor's.
Offset pointerPosition(bool pressed) {
  if (pointerLeft && !pressed) return {-1.0f, -1.0f};
  return trackedPointer;
}

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

Offset pointerPosition(bool) {
  const Vector2 mouse = GetMousePosition();
  return {mouse.x, mouse.y};
}

/// A desktop window redraws faster than a button can be pressed and released, so
/// the edges raylib reports between two frames are the whole story.
void collectButtons(Offset position) {
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    buttons.push_back({.phase = PointerPhase::Down, .position = position});
  }
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    buttons.push_back({.phase = PointerPhase::Up, .position = position});
  }
}

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
  emscripten_set_mousedown_callback("#canvas", nullptr, EM_FALSE, accumulateButton);
  emscripten_set_mouseup_callback("#canvas", nullptr, EM_FALSE, accumulateButton);
  emscripten_set_mousemove_callback("#canvas", nullptr, EM_FALSE, trackPointer);
  emscripten_set_mouseleave_callback("#canvas", nullptr, EM_FALSE, trackPointer);
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
  const Offset position = pointerPosition(pointerDown_);
  const bool moved = position != lastPointer_;
  const KeyModifiers modifiers = currentModifiers();

  collectButtons(position);
  if (moved || !buttons.empty()) keyboardMode_.set(false);

  // The motion first, because it is what carried the pointer to wherever it was
  // pressed. Hover and Move are different events, not the same one with a flag:
  // hover drives enter/exit, and a move during a press is routed to whatever
  // recognizer won the arena.
  if (moved) {
    binding.dispatchPointer(
        PointerEvent{.phase = pointerDown_ ? PointerPhase::Move : PointerPhase::Hover,
                     .pointer = 1,
                     .kind = PointerDeviceKind::Mouse,
                     .position = position,
                     .modifiers = modifiers});
  }

  // Every press and release, rather than whichever one a frame happened to catch
  // -- a click that begins and ends inside one frame is still a click.
  for (const ButtonEvent& button : buttons) {
    pointerDown_ = button.phase == PointerPhase::Down;
    binding.dispatchPointer(PointerEvent{.phase = button.phase,
                                         .pointer = 1,
                                         .kind = PointerDeviceKind::Mouse,
                                         .position = button.position,
                                         .modifiers = modifiers});
  }
  buttons.clear();

  lastPointer_ = position;
}

void Input::pumpSignals(fltr::WidgetBinding& binding) {
  // The conversion to the pixels the surface is measured in belongs to the
  // platform, so it happens here rather than inside the framework.
  const Offset wheel = takeWheelDelta();
  if (wheel == Offset{}) return;

  // The same position the hover is dispatched at, because the notch belongs to
  // whatever the cursor is over.
  const Offset position = pointerPosition(true);
  binding.dispatchSignal(PointerSignalEvent{.kind = PointerSignalKind::Scroll,
                                            .pointer = 1,
                                            .position = position,
                                            .localPosition = position,
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
