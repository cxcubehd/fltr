#include "fltr/components/toggle.hpp"

#include <algorithm>

namespace fltr {

namespace {

bool isActivator(LogicalKey key) noexcept {
  return key == LogicalKey::Space || key == LogicalKey::Enter;
}

float fractionFor(ToggleValue value) noexcept {
  switch (value) {
    case ToggleValue::Off: return 0.0f;
    case ToggleValue::On: return 1.0f;
    case ToggleValue::Mixed: return 0.5f;
  }
  return 0.0f;
}

}  // namespace

void RawToggleState::initState() {
  driver_.attach(context().tickers());
  adoptConfiguration();
  holdPosition(fractionFor(widget().args().value));
}

void RawToggleState::didUpdateWidget(const RawToggle& previous) {
  adoptConfiguration();
  if (previous.args().value == widget().args().value) return;
  // The consumer accepted a report, so the position is already on its way to
  // wherever the drag left it -- settling separately would send it twice.
  settleAfterDrag_ = false;
  position_.retarget(fractionFor(widget().args().value));
}

void RawToggleState::dispose() {
  states_.release();
  driver_.detach();
}

void RawToggleState::adoptConfiguration() {
  const RawToggle::Args& args = widget().args();
  states_.bind(args.statesController);
  driver_.setConfig({.duration = args.positionDuration});
  states_.update(WidgetState::Disabled, !args.enabled);
  states_.update(WidgetState::Selected, args.value != ToggleValue::Off);
  if (args.enabled) return;
  keyHeld_ = false;
  states_.update(WidgetState::Pressed, false);
  states_.update(WidgetState::Hovered, false);
  states_.update(WidgetState::Dragged, false);
}

ToggleValue RawToggleState::nextValue() const noexcept {
  const RawToggle::Args& args = widget().args();
  if (args.tristate) {
    switch (args.value) {
      case ToggleValue::Off: return ToggleValue::On;
      case ToggleValue::On: return ToggleValue::Mixed;
      case ToggleValue::Mixed: return ToggleValue::Off;
    }
  }
  if (args.value != ToggleValue::On) return ToggleValue::On;
  return args.canToggleOff ? ToggleValue::Off : ToggleValue::On;
}

void RawToggleState::toggle() {
  const ToggleValue next = nextValue();
  if (next == widget().args().value || !widget().args().onChanged) return;
  widget().args().onChanged(next);
}

void RawToggleState::endDrag() {
  states_.update(WidgetState::Dragged, false);
  const ToggleValue settled = position_.value() >= 0.5f ? ToggleValue::On : ToggleValue::Off;
  // A build is asked for whatever happens next, so the position always finds its
  // way back to the value: if the report is accepted the new one arrives first,
  // and if it is refused -- or was never a change -- this is what returns the
  // thumb to where the value actually is.
  setState([this] { settleAfterDrag_ = true; });
  if (settled != widget().args().value && widget().args().onChanged) {
    widget().args().onChanged(settled);
  }
}

bool RawToggleState::handleKey(const KeyEvent& event) {
  if (!isActivator(event.logical)) return false;
  if (event.type == KeyEventType::Repeat) return true;
  if (event.type == KeyEventType::Down) {
    keyHeld_ = true;
    states_.update(WidgetState::Pressed, true);
    return true;
  }
  if (!keyHeld_) return false;
  keyHeld_ = false;
  states_.update(WidgetState::Pressed, false);
  toggle();
  return true;
}

void RawToggleState::handleFocusChange(bool focused) {
  states_.update(WidgetState::Focused, focused);
  if (focused || !keyHeld_) return;
  keyHeld_ = false;
  states_.update(WidgetState::Pressed, false);
}

WidgetRef RawToggleState::build(BuildContext&) {
  const RawToggle::Args& args = widget().args();
  if (settleAfterDrag_) {
    settleAfterDrag_ = false;
    position_.retarget(fractionFor(args.value));
  }

  Pointer::Args pointer{
      .behavior = args.behavior,
      .cursor = args.cursor,
      .buttons = args.buttons,
      .onTapDown = [this] { states_.update(WidgetState::Pressed, true); },
      .onTap =
          [this] {
            states_.update(WidgetState::Pressed, false);
            toggle();
          },
      .onTapCancel = [this] { states_.update(WidgetState::Pressed, false); },
      .child = args.child,
  };
  if (args.dragExtent > 0.0f) {
    pointer.dragAxis = DragAxis::Horizontal;
    // The thumb keeps up with the finger exactly, slop included: a switch whose
    // thumb lagged the first eighteen pixels of every drag would feel stuck.
    pointer.dragStartBehavior = DragStartBehavior::Down;
    pointer.onDragStart = [this](const DragStartDetails&) {
      // The thumb is the finger's from here, so whatever the driver was doing
      // with it stops rather than fighting for the same value.
      driver_.stop();
      states_.update(WidgetState::Dragged, true);
    };
    pointer.onDragUpdate = [this](const DragUpdateDetails& details) {
      const float travelled = details.primaryDelta / widget().args().dragExtent;
      holdPosition(std::clamp(position_.value() + travelled, 0.0f, 1.0f));
    };
    pointer.onDragEnd = [this](const DragEndDetails&) { endDrag(); };
    pointer.onDragCancel = [this] { endDrag(); };
  }

  return buildComponentShell({
      .states = &states_,
      .fraction = &position_,
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
