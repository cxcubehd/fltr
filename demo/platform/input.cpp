#include "platform/input.hpp"

#include "raylib.h"

namespace fltrdemo {
namespace {

constexpr fltr::PointerId kMouse = 1;

fltr::PointerEvent event(fltr::PointerPhase phase, fltr::Offset position) noexcept {
  return {phase, kMouse, fltr::PointerDeviceKind::Mouse, position};
}

}  // namespace

void InputPump::syncSurface(fltr::WidgetBinding& binding) {
  if (!IsWindowResized()) return;
  binding.setSurface({static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
}

void InputPump::pump(fltr::WidgetBinding& binding, PointerRouter& router,
                     float scrollLineHeight) {
  const Vector2 mouse = GetMousePosition();
  const fltr::Offset position{mouse.x, mouse.y};

  const bool focused = IsWindowFocused();
  if (!focused && focused_) {
    // Withdrawn rather than released: the framework tells every recognizer and
    // every hovered region that the gesture is not going to finish.
    binding.dispatchPointer(event(fltr::PointerPhase::Cancel, position));
    router.cancel();
    pressed_ = false;
  }
  focused_ = focused;
  if (!focused) return;

  if (!hasPosition_ || position != last_) {
    binding.dispatchPointer(
        event(pressed_ ? fltr::PointerPhase::Move : fltr::PointerPhase::Hover, position));
    if (pressed_) router.move(position);
    last_ = position;
    hasPosition_ = true;
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    binding.dispatchPointer(event(fltr::PointerPhase::Down, position));
    pressed_ = true;
    router.down(binding, position);
  }
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    binding.dispatchPointer(event(fltr::PointerPhase::Up, position));
    pressed_ = false;
    router.up();
  }

  if (const float wheel = GetMouseWheelMove(); wheel != 0.0f) {
    router.wheel(binding, position, wheel, scrollLineHeight);
  }
}

}  // namespace fltrdemo
