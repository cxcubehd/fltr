#include "fltr/components/button.hpp"

namespace fltr {

namespace {

/// What activates a control from the keyboard. Flutter routes both through
/// `ActivateIntent`; here they are named where they are consumed, because there
/// is no `Intent` indirection between the stroke and what it does.
bool isActivator(LogicalKey key) noexcept {
  return key == LogicalKey::Space || key == LogicalKey::Enter;
}

}  // namespace

void RawButtonState::initState() { adoptConfiguration(); }

void RawButtonState::didUpdateWidget(const RawButton&) { adoptConfiguration(); }

void RawButtonState::dispose() { states_.release(); }

void RawButtonState::adoptConfiguration() {
  const RawButton::Args& args = widget().args();
  states_.bind(args.statesController);
  states_.update(WidgetState::Disabled, !args.enabled);
  states_.update(WidgetState::Selected, args.selected);
  if (args.enabled) return;
  // Whatever was in flight when the button was disabled did not finish, and no
  // event will arrive to say so: the guard above it stops the pointer, and the
  // focus is being taken away.
  keyHeld_ = false;
  setPressed(false);
  states_.update(WidgetState::Hovered, false);
}

bool RawButtonState::handleKey(const KeyEvent& event) {
  if (!isActivator(event.logical)) return false;
  // A held activator is one press, not a stream of them.
  if (event.type == KeyEventType::Repeat) return true;
  if (event.type == KeyEventType::Down) {
    keyHeld_ = true;
    setPressed(true);
    return true;
  }
  if (!keyHeld_) return false;
  keyHeld_ = false;
  setPressed(false);
  if (widget().args().onPressed) widget().args().onPressed();
  return true;
}

void RawButtonState::handleFocusChange(bool focused) {
  states_.update(WidgetState::Focused, focused);
  if (focused || !keyHeld_) return;
  keyHeld_ = false;
  setPressed(false);
}

WidgetRef RawButtonState::build(BuildContext&) {
  const RawButton::Args& args = widget().args();

  Pointer::Args pointer{
      .behavior = args.behavior,
      .cursor = args.cursor,
      .buttons = args.buttons,
      .onTapDown = [this] { setPressed(true); },
      .onTap =
          [this] {
            setPressed(false);
            if (widget().args().onPressed) widget().args().onPressed();
          },
      .onTapCancel = [this] { setPressed(false); },
      .child = args.child,
  };
  if (args.onLongPress) {
    // Where a two-gesture button's press feedback starts. `onTapDown` fires on
    // *winning* the arena, so while a long press is still contending the tap has
    // not won and nothing has reported the finger yet.
    pointer.onLongPress = [this](const LongPressStartDetails&) {
      setPressed(true);
      if (widget().args().onLongPress) widget().args().onLongPress();
    };
    pointer.onLongPressEnd = [this](const LongPressEndDetails&) { setPressed(false); };
    pointer.onLongPressCancel = [this] { setPressed(false); };
  }

  return buildComponentShell({
      .states = &states_,
      .enabled = args.enabled,
      .focusNode = args.focusNode,
      .canRequestFocus = args.canRequestFocus,
      .skipTraversal = args.skipTraversal,
      .autofocus = args.autofocus,
      .onKey = [this](const KeyEvent& event) { return handleKey(event); },
      .onFocusChange = [this](bool focused) { handleFocusChange(focused); },
      .pointer = pointer,
  });
}

}  // namespace fltr
