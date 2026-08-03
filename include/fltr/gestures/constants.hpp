#pragma once

namespace fltr {

/// How far a pointer may travel on a gesture's axis and still be a tap.
inline constexpr float kTouchSlop = 18.0f;
/// A drag with no axis has to clear a wider threshold, since travel in any
/// direction counts towards it.
inline constexpr float kPanSlop = kTouchSlop * 2.0f;

/// The same two, for a pointer that reports where it is to the pixel. A finger
/// earns the wider figure -- it is broad, and it rolls as it presses -- and a
/// mouse does not: eighteen pixels of one is a deliberate gesture, and a control
/// whose whole travel is shorter than that could never be dragged at all.
inline constexpr float kPreciseSlop = 1.0f;
inline constexpr float kPrecisePanSlop = 2.0f;

/// How long a press must be held, in seconds, before it is a long press.
inline constexpr float kLongPressTimeout = 0.5f;

/// How long the second tap of a double tap may take to arrive.
inline constexpr float kDoubleTapTimeout = 0.3f;
/// How far apart the two taps may land. Wider than the touch slop: the hand
/// lifts and returns between them, rather than staying on the surface.
inline constexpr float kDoubleTapSlop = 100.0f;

/// Below this a release is not a fling.
inline constexpr float kMinFlingVelocity = 50.0f;
/// Above this the estimate is noise from a very short sample window.
inline constexpr float kMaxFlingVelocity = 8000.0f;

}  // namespace fltr
