#pragma once

#include <cstdint>

#include "fltr/core/flags.hpp"
#include "fltr/core/geometry.hpp"

namespace fltr {

using PointerId = std::uint32_t;

/// What the consumer reports each time a pointer does something.
///
/// There is no timestamp: time enters this layer through the frame, exactly as
/// it enters the ticker, so a recognizer measuring a duration reads the gesture
/// clock the binding advances rather than one of its own.
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

enum class PointerButton : std::uint8_t { Primary = 1, Secondary = 2, Tertiary = 4 };
template <>
inline constexpr bool kIsFlags<PointerButton> = true;
using PointerButtons = Flags<PointerButton>;

enum class KeyModifier : std::uint8_t { Shift = 1, Control = 2, Alt = 4, Meta = 8 };
template <>
inline constexpr bool kIsFlags<KeyModifier> = true;
using KeyModifiers = Flags<KeyModifier>;

struct PointerEvent {
  PointerPhase phase = PointerPhase::Hover;
  PointerId pointer = 0;
  PointerDeviceKind kind = PointerDeviceKind::Mouse;
  /// In the view's coordinate space.
  Offset position;
  /// Which buttons are held once this event has happened, not which changed.
  /// The default is the primary button, so the common case reports nothing and a
  /// touch pointer needs no button model at all.
  PointerButtons buttons{PointerButton::Primary};
  KeyModifiers modifiers;
};

/// A wheel notch or a trackpad pan: something a pointer reports without pressing
/// anything, so there is nothing to contest and no arena to contest it in.
enum class PointerSignalKind : std::uint8_t {
  Scroll,  ///< a wheel, in discrete notches
  Pan,     ///< two fingers on a trackpad, already in physical pixels
};

struct PointerSignalEvent {
  PointerSignalKind kind = PointerSignalKind::Scroll;
  PointerId pointer = 0;
  /// In the view's coordinate space.
  Offset position;
  /// The same point in the space of whichever region is being offered this
  /// signal; filled in by the binding on the way down.
  Offset localPosition;
  /// How far the content should move, in logical pixels. A wheel notch is
  /// already converted by the consumer, which is the only place that knows the
  /// platform's notch size.
  Offset delta;
  KeyModifiers modifiers;
};

/// What the cursor should look like over a region. `Defer` is the absence of an
/// opinion, so whatever is painted behind decides.
enum class MouseCursor : std::uint8_t {
  Defer,
  None,
  Basic,
  Click,
  Text,
  Forbidden,
  Grab,
  Grabbing,
  Move,
  Progress,
  ResizeLeftRight,
  ResizeUpDown,
};

/// How a region takes part in hit testing, which is what decides whether
/// anything painted behind it is reachable.
enum class HitTestBehavior : std::uint8_t {
  DeferToChild,  ///< hit only where a child was hit
  Opaque,        ///< hit anywhere within bounds, and nothing behind it is reached
  Translucent,   ///< hit anywhere within bounds, and what is behind is reached too
};

}  // namespace fltr
