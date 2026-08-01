#pragma once

#include <cstdint>

#include "fltr/gestures/events.hpp"

namespace fltr {

enum class KeyEventType : std::uint8_t { Down, Up, Repeat };

/// Where the key sits on the keyboard, independent of layout. This is what
/// movement binds to: WASD stays under the same fingers on AZERTY.
enum class PhysicalKey : std::uint16_t {
  None = 0,
  KeyA, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI, KeyJ, KeyK, KeyL, KeyM,
  KeyN, KeyO, KeyP, KeyQ, KeyR, KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,
  Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  Escape, Enter, Tab, Space, Backspace, Delete, Insert,
  Home, End, PageUp, PageDown,
  ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
  Minus, Equal, BracketLeft, BracketRight, Backslash, Semicolon, Quote,
  Backquote, Comma, Period, Slash,
  ShiftLeft, ShiftRight, ControlLeft, ControlRight, AltLeft, AltRight,
  MetaLeft, MetaRight, CapsLock,
  NumpadDivide, NumpadMultiply, NumpadSubtract, NumpadAdd, NumpadEnter,
  NumpadDecimal, Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6,
  Numpad7, Numpad8, Numpad9,
};

/// What the key means under the active layout, which is what a shortcut binds
/// to. Kept a separate enum from `PhysicalKey` for the reason Flutter keeps them
/// separate: the same position produces different meanings, and the same meaning
/// comes from different positions.
enum class LogicalKey : std::uint16_t {
  None = 0,
  KeyA, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI, KeyJ, KeyK, KeyL, KeyM,
  KeyN, KeyO, KeyP, KeyQ, KeyR, KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,
  Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  Escape, Enter, Tab, Space, Backspace, Delete, Insert,
  Home, End, PageUp, PageDown,
  ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
  Minus, Equal, BracketLeft, BracketRight, Backslash, Semicolon, Quote,
  Backquote, Comma, Period, Slash,
  Shift, Control, Alt, Meta, CapsLock,
};

struct KeyEvent {
  KeyEventType type = KeyEventType::Down;
  PhysicalKey physical = PhysicalKey::None;
  LogicalKey logical = LogicalKey::None;
  KeyModifiers modifiers;
  /// The code point this keystroke produces, or zero for a key that produces
  /// none. Which one that is depends on the layout and the IME, both of which
  /// live on the consumer's side, so it is reported rather than derived.
  char32_t character = 0;
};

}  // namespace fltr
