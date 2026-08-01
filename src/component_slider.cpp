#include "fltr/components/slider.hpp"

#include <algorithm>
#include <cmath>

namespace fltr {

void RawSliderState::initState() { adoptConfiguration(); }

void RawSliderState::didUpdateWidget(const RawSlider&) { adoptConfiguration(); }

void RawSliderState::dispose() { states_.release(); }

void RawSliderState::adoptConfiguration() {
  const RawSlider::Args& args = widget().args();
  FLTR_EXPECTS(args.max >= args.min, "a slider's range must not run backwards");
  states_.bind(args.statesController);
  states_.update(WidgetState::Disabled, !args.enabled);
  fraction_.set(fractionOf(args.value));
  if (args.enabled) return;
  states_.update(WidgetState::Pressed, false);
  states_.update(WidgetState::Hovered, false);
  states_.update(WidgetState::Dragged, false);
}

float RawSliderState::fractionOf(float value) const noexcept {
  const RawSlider::Args& args = widget().args();
  const float span = args.max - args.min;
  if (span <= 0.0f) return 0.0f;
  return std::clamp((value - args.min) / span, 0.0f, 1.0f);
}

float RawSliderState::valueOf(float fraction) const noexcept {
  const RawSlider::Args& args = widget().args();
  if (args.divisions > 0) {
    const float steps = static_cast<float>(args.divisions);
    fraction = std::round(fraction * steps) / steps;
  }
  return args.min + fraction * (args.max - args.min);
}

float RawSliderState::keyStep() const noexcept {
  const RawSlider::Args& args = widget().args();
  return args.divisions > 0 ? 1.0f / static_cast<float>(args.divisions) : args.keyStep;
}

float RawSliderState::trackExtent() const {
  const RenderBox* box = context().element().renderObject();
  if (!box || !box->hasSize()) return 0.0f;
  const Size size = box->size();
  const RawSlider::Args& args = widget().args();
  const float along = args.axis == Axis::Horizontal ? size.width : size.height;
  return std::max(along - args.thumbExtent, 0.0f);
}

void RawSliderState::report(float value) {
  const RawSlider::Args& args = widget().args();
  const float clamped = std::clamp(value, args.min, args.max);
  if (clamped == args.value || !args.onChanged) return;
  args.onChanged(clamped);
}

void RawSliderState::moveTo(Offset local) {
  const float extent = trackExtent();
  if (extent <= 0.0f) return;
  const RawSlider::Args& args = widget().args();
  const float along = args.axis == Axis::Horizontal ? local.dx : local.dy;
  report(valueOf(std::clamp((along - args.thumbExtent * 0.5f) / extent, 0.0f, 1.0f)));
}

void RawSliderState::nudge(float byFraction) {
  report(valueOf(std::clamp(fraction_.value() + byFraction, 0.0f, 1.0f)));
}

bool RawSliderState::handleKey(const KeyEvent& event) {
  if (event.type == KeyEventType::Up) return false;
  const RawSlider::Args& args = widget().args();
  const bool horizontal = args.axis == Axis::Horizontal;
  // Only the two arrows along this slider's own axis are consumed; the pair
  // across it is left alone, which is what still lets a D-pad leave a row of
  // sliders rather than being trapped in one.
  const LogicalKey back = horizontal ? LogicalKey::ArrowLeft : LogicalKey::ArrowUp;
  const LogicalKey forward = horizontal ? LogicalKey::ArrowRight : LogicalKey::ArrowDown;
  if (event.logical == back || event.logical == forward) {
    nudge(event.logical == back ? -keyStep() : keyStep());
    return true;
  }
  switch (event.logical) {
    case LogicalKey::PageUp: nudge(args.pageStep); return true;
    case LogicalKey::PageDown: nudge(-args.pageStep); return true;
    case LogicalKey::Home: report(args.min); return true;
    case LogicalKey::End: report(args.max); return true;
    default: return false;
  }
}

void RawSliderState::handleFocusChange(bool focused) {
  states_.update(WidgetState::Focused, focused);
}

WidgetRef RawSliderState::build(BuildContext&) {
  const RawSlider::Args& args = widget().args();

  Pointer::Args pointer{
      .behavior = args.behavior,
      .cursor = args.cursor,
      .buttons = args.buttons,
      // Measured from the down point rather than from where the slop was
      // cleared: the value is read off the pointer's position, so the drag must
      // start where the finger already is.
      .dragAxis = args.axis == Axis::Horizontal ? DragAxis::Horizontal : DragAxis::Vertical,
      .dragStartBehavior = DragStartBehavior::Down,
      .onDragDown =
          [this](const DragDownDetails& details) {
            states_.update(WidgetState::Pressed, true);
            if (widget().args().onChangeStart) widget().args().onChangeStart(widget().args().value);
            moveTo(details.localPosition);
          },
      .onDragStart = [this](const DragStartDetails&) { states_.update(WidgetState::Dragged, true); },
      .onDragUpdate =
          [this](const DragUpdateDetails& details) { moveTo(details.localPosition); },
      .onDragEnd =
          [this](const DragEndDetails&) {
            states_.update(WidgetState::Pressed, false);
            states_.update(WidgetState::Dragged, false);
            if (widget().args().onChangeEnd) widget().args().onChangeEnd(widget().args().value);
          },
      .onDragCancel =
          [this] {
            states_.update(WidgetState::Pressed, false);
            states_.update(WidgetState::Dragged, false);
            if (widget().args().onChangeEnd) widget().args().onChangeEnd(widget().args().value);
          },
      .child = args.child,
  };

  return buildComponentShell({
      .states = &states_,
      .fraction = &fraction_,
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
