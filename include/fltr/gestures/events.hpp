#pragma once

#include <cstdint>

#include "fltr/core/geometry.hpp"

namespace fltr {

using PointerId = std::uint32_t;

/// What the consumer reports each time a pointer does something.
///
/// There is no button model: the game decides what counts as a press, exactly as
/// it decides what counts as a frame. There is no timestamp either -- nothing in
/// this layer reads a clock, and time enters the framework with the ticker.
enum class PointerPhase : std::uint8_t {
  Hover,   ///< moved with nothing pressed
  Down,
  Move,    ///< moved while pressed
  Up,
  Cancel,  ///< withdrawn by the consumer: window focus lost, device removed
};

/// Only a mouse hovers. A touch pointer is not tracked between its down and its
/// up, so it never produces enter or exit.
enum class PointerDeviceKind : std::uint8_t { Mouse, Touch };

struct PointerEvent {
  PointerPhase phase = PointerPhase::Hover;
  PointerId pointer = 0;
  PointerDeviceKind kind = PointerDeviceKind::Mouse;
  /// In the view's coordinate space.
  Offset position;
};

/// How a region takes part in hit testing, which is what decides whether
/// anything painted behind it is reachable.
enum class HitTestBehavior : std::uint8_t {
  DeferToChild,  ///< hit only where a child was hit
  Opaque,        ///< hit anywhere within bounds, and nothing behind it is reached
  Translucent,   ///< hit anywhere within bounds, and what is behind is reached too
};

}  // namespace fltr
