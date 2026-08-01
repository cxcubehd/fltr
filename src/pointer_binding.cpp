#include "fltr/gestures/binding.hpp"

#include <algorithm>

#include "fltr/gestures/pointer_region.hpp"
#include "fltr/gestures/recognizer.hpp"

namespace fltr {

const HitTestResult& PointerBinding::hitTest(RenderBox& root, Offset position) {
  ++hitTests_;
  path_.clear();
  root.hitTest(path_, position);
  return path_;
}

void PointerBinding::dispatch(const PointerEvent& event, RenderBox& root) {
  DispatchScope scope(*this);
  const bool tracksHover = event.kind == PointerDeviceKind::Mouse;

  if (event.phase == PointerPhase::Cancel) {
    routeToRecognizers(event);
    arena_.cancel(event.pointer);
    if (tracksHover) tracker_.clear();
    return;
  }

  if (event.phase == PointerPhase::Down || tracksHover) hitTest(root, event.position);

  switch (event.phase) {
    case PointerPhase::Down:
      // Members join deepest-first, which is the order the arena awards an
      // unresolved sweep in.
      for (const HitTestEntry& entry : path_.path()) {
        RenderPointerRegion* region = entry.target->asPointerRegion();
        if (region) region->receiveDown(event);
      }
      arena_.close(event.pointer);
      break;
    case PointerPhase::Up:
      routeToRecognizers(event);
      arena_.sweep(event.pointer);
      break;
    case PointerPhase::Move:
      routeToRecognizers(event);
      break;
    case PointerPhase::Hover:
    case PointerPhase::Cancel:
      break;
  }

  if (tracksHover) tracker_.update(event.position, path_);
}

bool PointerBinding::dispatchSignal(const PointerSignalEvent& event, RenderBox& root) {
  DispatchScope scope(*this);
  const HitTestResult& path = hitTest(root, event.position);
  for (const HitTestEntry& entry : path.path()) {
    RenderPointerRegion* region = entry.target->asPointerRegion();
    if (!region) continue;
    PointerSignalEvent local = event;
    local.localPosition = entry.localPosition;
    if (region->receiveSignal(local)) return true;
  }
  return false;
}

void PointerBinding::settleHover(RenderBox& root) {
  if (!tracker_.needsResolve()) return;
  DispatchScope scope(*this);
  const Offset position = tracker_.position();
  hitTest(root, position);
  tracker_.update(position, path_);
}

// ---------------------------------------------------------------------------
// The gesture clock
// ---------------------------------------------------------------------------

void PointerBinding::advanceTime(float seconds) {
  FLTR_EXPECTS(seconds >= 0.0f, "a frame's elapsed time may not be negative");
  now_ += seconds;
  if (timeouts_.empty()) return;

  // Due deadlines are taken out of the list before any of them fires, so a
  // callback that schedules another deadline cannot have it fire in this pass.
  firing_.clear();
  std::size_t kept = 0;
  for (const Timeout& timeout : timeouts_) {
    if (timeout.deadline > now_) {
      timeouts_[kept++] = timeout;
    } else {
      firing_.push_back(timeout);
    }
  }
  timeouts_.resize(kept);

  // Insertion sort, not std::stable_sort: a frame swallows one or two deadlines,
  // and the library sort allocates a temporary buffer -- which the rule against
  // per-frame heap churn does not allow, however small the frame's work is.
  for (std::size_t i = 1; i < firing_.size(); ++i) {
    const Timeout entry = firing_[i];
    std::size_t j = i;
    for (; j > 0 && firing_[j - 1].deadline > entry.deadline; --j) firing_[j] = firing_[j - 1];
    firing_[j] = entry;
  }

  for (const Timeout& timeout : firing_) {
    if (timeout.recognizer) timeout.recognizer->handleTimeout(timeout.pointer);
  }
  firing_.clear();
}

void PointerBinding::scheduleTimeout(GestureRecognizer& recognizer, PointerId pointer,
                                     float delay) {
  timeouts_.push_back({now_ + delay, pointer, &recognizer});
}

void PointerBinding::cancelTimeouts(GestureRecognizer& recognizer) {
  std::erase_if(timeouts_, [&](const Timeout& t) { return t.recognizer == &recognizer; });
  for (Timeout& timeout : firing_) {
    if (timeout.recognizer == &recognizer) timeout.recognizer = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

void PointerBinding::routeToRecognizers(const PointerEvent& event) {
  routing_.clear();
  for (const Route& route : routes_) {
    if (route.pointer == event.pointer) routing_.push_back(route);
  }
  for (std::size_t i = 0; i < routing_.size(); ++i) {
    if (GestureRecognizer* recognizer = routing_[i].recognizer) recognizer->handleEvent(event);
  }
  routing_.clear();
}

void PointerBinding::addRoute(PointerId pointer, GestureRecognizer& recognizer) {
  routes_.push_back({pointer, &recognizer});
}

void PointerBinding::removeRoute(PointerId pointer, GestureRecognizer& recognizer) {
  std::erase_if(routes_, [&](const Route& route) {
    return route.pointer == pointer && route.recognizer == &recognizer;
  });
  for (Route& route : routing_) {
    if (route.pointer == pointer && route.recognizer == &recognizer) route.recognizer = nullptr;
  }
}

void PointerBinding::removeRoutes(GestureRecognizer& recognizer) {
  std::erase_if(routes_, [&](const Route& route) { return route.recognizer == &recognizer; });
  for (Route& route : routing_) {
    if (route.recognizer == &recognizer) route.recognizer = nullptr;
  }
}

// ---------------------------------------------------------------------------
// MouseTracker
// ---------------------------------------------------------------------------

namespace {

bool holds(const std::vector<RenderPointerRegion*>& regions, const RenderPointerRegion* region) {
  return std::find(regions.begin(), regions.end(), region) != regions.end();
}

}  // namespace

void MouseTracker::update(Offset position, const HitTestResult& path) {
  position_ = position;
  hasCursor_ = true;
  stale_ = false;

  scratch_.clear();
  for (const HitTestEntry& entry : path.path()) {
    RenderPointerRegion* region = entry.target->asPointerRegion();
    if (region && region->tracksMouse()) scratch_.push_back(region);
  }

  // Publish the new answer before any callback runs, so a callback asking what
  // is hovered gets the current answer rather than the one being replaced.
  hovered_.swap(scratch_);
  resolveCursor();
  for (RenderPointerRegion* region : scratch_) {
    if (!holds(hovered_, region)) region->exit();
  }
  for (RenderPointerRegion* region : hovered_) {
    if (!holds(scratch_, region)) region->enter();
  }
  scratch_.clear();
}

void MouseTracker::clear() {
  hasCursor_ = false;
  stale_ = false;
  scratch_.clear();
  hovered_.swap(scratch_);
  resolveCursor();
  for (RenderPointerRegion* region : scratch_) region->exit();
  scratch_.clear();
}

void MouseTracker::forget(RenderPointerRegion& region) {
  std::erase(hovered_, &region);
  std::erase(scratch_, &region);
  resolveCursor();
  invalidate();
}

void MouseTracker::resolveCursor() noexcept {
  // The list is deepest first, so the innermost region with an opinion wins and
  // everything painted behind it defers to it.
  for (const RenderPointerRegion* region : hovered_) {
    if (region->cursor() != MouseCursor::Defer) {
      cursor_ = region->cursor();
      return;
    }
  }
  cursor_ = MouseCursor::Basic;
}

// ---------------------------------------------------------------------------
// RenderPointerRegion
// ---------------------------------------------------------------------------

RenderPointerRegion::RenderPointerRegion(PointerBinding& binding, HitTestBehavior behavior)
    : binding_(&binding), behavior_(behavior) {}

RenderPointerRegion::~RenderPointerRegion() { binding_->mouseTracker().forget(*this); }

std::string RenderPointerRegion::describe() const { return hovered_ ? "hovered" : std::string(); }

std::size_t RenderPointerRegion::recognizerCount() const noexcept {
  return (tap_ ? 1u : 0u) + (doubleTap_ ? 1u : 0u) + (longPress_ ? 1u : 0u) + (drag_ ? 1u : 0u);
}

void RenderPointerRegion::setBehavior(HitTestBehavior behavior) {
  if (behavior == behavior_) return;
  behavior_ = behavior;
  // Neither layout nor paint changes, but what lies under the cursor may have.
  binding_->mouseTracker().invalidate();
}

void RenderPointerRegion::setCursor(MouseCursor cursor) {
  if (cursor == cursor_) return;
  cursor_ = cursor;
  // Both what the cursor should look like where it already is, and whether this
  // region is tracked at all, may have just changed.
  binding_->mouseTracker().invalidate();
}

template <class R>
R* RenderPointerRegion::syncRecognizer(std::unique_ptr<R>& slot, bool wanted,
                                       PointerButtons buttons) {
  if (!wanted) {
    slot.reset();
    return nullptr;
  }
  if (!slot) {
    slot = std::make_unique<R>(*binding_);
    slot->setCoordinateSpace(this);
  }
  slot->allowedButtons = buttons;
  return slot.get();
}

void RenderPointerRegion::setCallbacks(const PointerCallbacks& callbacks) {
  const bool trackedMouse = tracksMouse();
  onEnter_ = callbacks.onEnter;
  onExit_ = callbacks.onExit;
  onSignal_ = callbacks.onSignal;

  if (TapGestureRecognizer* tap = syncRecognizer(
          tap_, callbacks.onTapDown || callbacks.onTap || callbacks.onTapCancel,
          callbacks.buttons)) {
    tap->onTapDown = callbacks.onTapDown;
    tap->onTap = callbacks.onTap;
    tap->onTapCancel = callbacks.onTapCancel;
  }

  if (DoubleTapGestureRecognizer* doubleTap =
          syncRecognizer(doubleTap_, static_cast<bool>(callbacks.onDoubleTap), callbacks.buttons)) {
    doubleTap->onDoubleTap = callbacks.onDoubleTap;
  }

  if (LongPressGestureRecognizer* longPress = syncRecognizer(
          longPress_,
          callbacks.onLongPress || callbacks.onLongPressMoveUpdate || callbacks.onLongPressEnd ||
              callbacks.onLongPressCancel,
          callbacks.buttons)) {
    longPress->onStart = callbacks.onLongPress;
    longPress->onMoveUpdate = callbacks.onLongPressMoveUpdate;
    longPress->onEnd = callbacks.onLongPressEnd;
    longPress->onCancel = callbacks.onLongPressCancel;
  }

  if (DragGestureRecognizer* drag = syncRecognizer(
          drag_,
          callbacks.onDragDown || callbacks.onDragStart || callbacks.onDragUpdate ||
              callbacks.onDragEnd || callbacks.onDragCancel,
          callbacks.buttons)) {
    drag->axis = callbacks.dragAxis;
    drag->startBehavior = callbacks.dragStartBehavior;
    drag->onDown = callbacks.onDragDown;
    drag->onStart = callbacks.onDragStart;
    drag->onUpdate = callbacks.onDragUpdate;
    drag->onEnd = callbacks.onDragEnd;
    drag->onCancel = callbacks.onDragCancel;
  }

  if (trackedMouse != tracksMouse()) binding_->mouseTracker().invalidate();
}

bool RenderPointerRegion::hitTest(HitTestResult& result, Offset position) {
  if (!hasSize() || !paintBounds().contains(position)) return false;
  const bool child = hitTestChildren(result, position);
  if (child || behavior_ != HitTestBehavior::DeferToChild) result.add(this, position);
  return child || behavior_ == HitTestBehavior::Opaque;
}

void RenderPointerRegion::receiveDown(const PointerEvent& down) {
  if (tap_) tap_->addPointer(down);
  if (doubleTap_) doubleTap_->addPointer(down);
  if (longPress_) longPress_->addPointer(down);
  if (drag_) drag_->addPointer(down);
}

bool RenderPointerRegion::receiveSignal(const PointerSignalEvent& signal) {
  return onSignal_ && onSignal_(signal);
}

void RenderPointerRegion::enter() {
  hovered_ = true;
  if (onEnter_) onEnter_();
}

void RenderPointerRegion::exit() {
  hovered_ = false;
  if (onExit_) onExit_();
}

}  // namespace fltr
