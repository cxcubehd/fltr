#pragma once

#include <cstddef>
#include <vector>

#include "fltr/gestures/keys.hpp"

namespace fltr {

/// Something that wants key events. Returning true consumes the event, so
/// nothing registered after this one sees it.
class KeyHandler {
public:
  virtual bool handleKey(const KeyEvent& event) = 0;

protected:
  ~KeyHandler() = default;
};

/// Where the consumer pushes key events, and what remembers which keys are held.
///
/// M9 builds the surface, not the routing: handlers are offered events in
/// registration order and there is no notion of what is focused. The focus
/// system registers itself as one more handler, so a game that never builds a
/// focus scope holds no handler here and pays nothing for the fact that one
/// exists.
///
/// The held-key set is worth having on its own: a game reads it directly for
/// movement, where an event stream is the wrong shape.
class KeyboardBinding {
public:
  KeyboardBinding() = default;

  KeyboardBinding(const KeyboardBinding&) = delete;
  KeyboardBinding& operator=(const KeyboardBinding&) = delete;

  /// One key event. Returns whether a handler consumed it.
  bool dispatch(const KeyEvent& event);

  void addHandler(KeyHandler& handler);
  void removeHandler(KeyHandler& handler);

  bool isPressed(PhysicalKey key) const noexcept;
  KeyModifiers modifiers() const noexcept { return modifiers_; }
  std::size_t pressedCount() const noexcept { return pressed_.size(); }

  /// Everything held is released, as when the window loses focus and the ups
  /// will never arrive.
  void clearPressed() noexcept;

private:
  std::vector<PhysicalKey> pressed_;
  std::vector<KeyHandler*> handlers_;
  /// The handlers of the event being delivered. One may withdraw itself or
  /// another from its own callback, so `removeHandler` blanks entries here
  /// rather than erasing them, keeping the walk's indices valid.
  std::vector<KeyHandler*> dispatching_;
  KeyModifiers modifiers_;
};

}  // namespace fltr
