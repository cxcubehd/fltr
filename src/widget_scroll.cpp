#include "fltr/widgets/scroll.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// Scrollable
// ---------------------------------------------------------------------------

void ScrollableState::initState() {
  position_.attach(context().tickers());
  adoptConfiguration();
  if (ScrollController* controller = position_.controller()) {
    position_.correctPixels(controller->initialOffset());
  }
}

void ScrollableState::didUpdateWidget(const Scrollable&) { adoptConfiguration(); }

void ScrollableState::dispose() {
  if (ScrollController* controller = position_.controller()) controller->detach();
  position_.detach();
}

void ScrollableState::adoptConfiguration() {
  position_.setAxis(widget().axis());
  position_.setPhysics(widget().physics());

  ScrollController* next = widget().controller();
  if (next == position_.controller()) return;
  if (next) {
    next->attach(position_);
  } else if (ScrollController* previous = position_.controller()) {
    previous->detach();
  }
}

WidgetRef ScrollableState::build(BuildContext&) {
  const bool vertical = widget().axis() == Axis::Vertical;
  return ScrollScope::make({
      .position = &position_,
      .child = Pointer::make({
          .behavior = HitTestBehavior::Opaque,
          .dragAxis = vertical ? DragAxis::Vertical : DragAxis::Horizontal,
          .onDragStart = [this](const DragStartDetails&) { position_.beginDrag(); },
          .onDragUpdate =
              [this](const DragUpdateDetails& details) {
                position_.applyDragDelta(details.primaryDelta);
              },
          .onDragEnd =
              [this](const DragEndDetails& details) { position_.endDrag(details.primaryVelocity); },
          .onDragCancel = [this] { position_.endDrag(0.0f); },
          .onSignal =
              [this](const PointerSignalEvent& signal) {
                const float delta =
                    position_.axis() == Axis::Vertical ? signal.delta.dy : signal.delta.dx;
                return signal.kind == PointerSignalKind::Scroll ? position_.applyWheelDelta(delta)
                                                                : position_.applyPanDelta(delta);
              },
          // The content is a repaint boundary, so a frame of scrolling
          // re-records the viewport's clip and one reference, not the content.
          .child = Viewport::make({
              .axis = widget().axis(),
              .position = &position_,
              .child = RepaintBoundary::make({.child = widget().child()}),
          }),
      }),
  });
}

// ---------------------------------------------------------------------------
// Scrollbar
// ---------------------------------------------------------------------------

void ScrollbarState::initState() {
  fade_.attach(context().tickers());
  idle_.attach(context().tickers());
  bindController(widget().args().controller);
}

void ScrollbarState::didUpdateWidget(const Scrollbar& previous) {
  if (widget().args().controller != previous.args().controller) {
    bindController(widget().args().controller);
  }
}

void ScrollbarState::dispose() {
  fade_.detach();
  idle_.detach();
  ScrollObserverState::dispose();
}

void ScrollbarState::didAttach() {
  setState([] {});
}

void ScrollbarState::didScroll() { show(); }

void ScrollbarState::show() {
  still_ = 0.0f;
  fade_.forward();
  if (widget().args().fadeDelay > 0.0f) idle_.start();
}

void ScrollbarState::countIdle(float seconds) {
  if (hovered_ || dragging_ || (position() && position()->isScrolling())) {
    still_ = 0.0f;
    return;
  }
  still_ += seconds;
  if (still_ < widget().args().fadeDelay) return;
  fade_.reverse();
  idle_.stop();
}

void ScrollbarState::setExpanded(bool hovered, bool dragging) {
  show();
  if (hovered == hovered_ && dragging == dragging_) return;
  setState([&] {
    hovered_ = hovered;
    dragging_ = dragging;
  });
}

ScrollbarGeometry ScrollbarState::geometry() const {
  return ScrollbarGeometry::resolve(position()->metrics(), position()->viewportDimension(),
                                    widget().args().minThumbExtent);
}

void ScrollbarState::grabAt(Offset local) {
  const bool vertical = position()->axis() == Axis::Vertical;
  const float along = vertical ? local.dy : local.dx;
  const ScrollbarGeometry thumb = geometry();
  grabbed_ = along >= thumb.start && along < thumb.start + thumb.extent;
  setExpanded(hovered_, grabbed_);
}

void ScrollbarState::dragThumb(float delta) {
  if (!grabbed_) return;
  const ScrollMetrics& metrics = position()->metrics();
  const float track = metrics.viewportDimension;
  position()->jumpTo(metrics.pixels + geometry().scrollForThumbDelta(metrics, track, delta));
}

/// The stack is here whether or not there is a bar yet, so the child keeps its
/// slot when the controller finds its scrollable a build later -- a subtree that
/// changes place has to be inflated afresh, and the configuration that built it
/// belongs to an arena that is already gone.
WidgetRef ScrollbarState::build(BuildContext&) {
  return Stack::make({
      .fit = StackFit::Expand,
      .children = {widget().child(), position() ? buildTrack() : WidgetRef{}},
  });
}

WidgetRef ScrollbarState::buildTrack() {
  const Scrollbar::Args& args = widget().args();
  const bool vertical = position()->axis() == Axis::Vertical;
  const float thickness = hovered_ || dragging_ ? args.hoveredThickness : args.thickness;

  // Opaque, so the strip answers to the pointer even while the thumb is faded
  // out -- otherwise a scrollbar that hides itself could never be revealed.
  const WidgetRef track = Pointer::make({
      .behavior = HitTestBehavior::Opaque,
      .onEnter = [this] { setExpanded(true, dragging_); },
      .onExit = [this] { setExpanded(false, dragging_); },
      .dragAxis = vertical ? DragAxis::Vertical : DragAxis::Horizontal,
      .dragStartBehavior = DragStartBehavior::Down,
      .onDragStart = [this](const DragStartDetails& details) { grabAt(details.localPosition); },
      .onDragUpdate =
          [this](const DragUpdateDetails& details) { dragThumb(details.primaryDelta); },
      .onDragEnd = [this](const DragEndDetails&) { setExpanded(hovered_, false); },
      .onDragCancel = [this] { setExpanded(hovered_, false); },
      .child = ScrollbarThumb::make({
          .position = position(),
          .axis = position()->axis(),
          .minExtent = args.minThumbExtent,
          .color = args.color,
          .radius = args.radius,
          .opacity = &visibility_,
      }),
  });

  return vertical ? Positioned::make({.top = 0.0f,
                                      .right = 0.0f,
                                      .bottom = 0.0f,
                                      .width = thickness,
                                      .child = track})
                  : Positioned::make({.left = 0.0f,
                                      .right = 0.0f,
                                      .bottom = 0.0f,
                                      .height = thickness,
                                      .child = track});
}

// ---------------------------------------------------------------------------
// Overscroll
// ---------------------------------------------------------------------------

void OverscrollState::initState() { bindController(widget().controller()); }

void OverscrollState::didUpdateWidget(const Overscroll& previous) {
  if (widget().controller() != previous.controller()) bindController(widget().controller());
}

void OverscrollState::didScroll() {
  const ScrollPosition& scroll = *position();
  const float refused = scroll.overscroll();
  const float viewport = scroll.viewportDimension();
  const float fraction =
      viewport > 0.0f ? std::clamp(std::fabs(refused) / viewport, 0.0f, 1.0f) : 0.0f;
  const float scale = 1.0f + fraction * widget().maxStretch();
  const bool vertical = scroll.axis() == Axis::Vertical;
  stretch_.set(vertical ? Transform2D::scaling(1.0f, scale) : Transform2D::scaling(scale, 1.0f));

  if (refused == 0.0f) return;
  // Anchored at the edge opposite the one being pushed, so the content
  // stretches away from the finger rather than sliding under it.
  const Alignment pivot =
      vertical ? (refused > 0.0f ? Alignment::topCenter() : Alignment::bottomCenter())
               : (refused > 0.0f ? Alignment::centerLeft() : Alignment::centerRight());
  if (pivot != pivot_) setState([&] { pivot_ = pivot; });
}

WidgetRef OverscrollState::build(BuildContext&) {
  return ClipRect::make({.child = Transform::make({
                             .animation = &stretch_,
                             .origin = pivot_,
                             .child = widget().child(),
                         })});
}

}  // namespace fltr
