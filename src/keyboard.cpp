#include "fltr/gestures/keyboard.hpp"

#include <algorithm>

namespace fltr {

bool KeyboardBinding::dispatch(const KeyEvent& event) {
  modifiers_ = event.modifiers;
  if (event.physical != PhysicalKey::None) {
    const auto it = std::find(pressed_.begin(), pressed_.end(), event.physical);
    if (event.type == KeyEventType::Up) {
      if (it != pressed_.end()) pressed_.erase(it);
    } else if (it == pressed_.end()) {
      pressed_.push_back(event.physical);
    }
  }

  dispatching_ = handlers_;
  bool consumed = false;
  for (KeyHandler* handler : dispatching_) {
    if (handler && handler->handleKey(event)) {
      consumed = true;
      break;
    }
  }
  dispatching_.clear();
  return consumed;
}

void KeyboardBinding::addHandler(KeyHandler& handler) { handlers_.push_back(&handler); }

void KeyboardBinding::removeHandler(KeyHandler& handler) {
  std::erase(handlers_, &handler);
  std::replace(dispatching_.begin(), dispatching_.end(), &handler,
               static_cast<KeyHandler*>(nullptr));
}

bool KeyboardBinding::isPressed(PhysicalKey key) const noexcept {
  return std::find(pressed_.begin(), pressed_.end(), key) != pressed_.end();
}

void KeyboardBinding::clearPressed() noexcept {
  pressed_.clear();
  modifiers_ = {};
}

}  // namespace fltr
