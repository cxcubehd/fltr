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

void PointerBinding::settleHover(RenderBox& root) {
  if (!tracker_.needsResolve()) return;
  DispatchScope scope(*this);
  const Offset cursor = tracker_.cursor();
  hitTest(root, cursor);
  tracker_.update(cursor, path_);
}

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
  cursor_ = position;
  hasCursor_ = true;
  stale_ = false;

  scratch_.clear();
  for (const HitTestEntry& entry : path.path()) {
    RenderPointerRegion* region = entry.target->asPointerRegion();
    if (region && region->wantsHover()) scratch_.push_back(region);
  }

  // Publish the new answer before any callback runs, so a callback asking what
  // is hovered gets the current answer rather than the one being replaced.
  hovered_.swap(scratch_);
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
  for (RenderPointerRegion* region : scratch_) region->exit();
  scratch_.clear();
}

void MouseTracker::forget(RenderPointerRegion& region) {
  std::erase(hovered_, &region);
  std::erase(scratch_, &region);
  invalidate();
}

// ---------------------------------------------------------------------------
// RenderPointerRegion
// ---------------------------------------------------------------------------

RenderPointerRegion::RenderPointerRegion(PointerBinding& binding, HitTestBehavior behavior)
    : binding_(&binding), tap_(binding), behavior_(behavior) {}

RenderPointerRegion::~RenderPointerRegion() { binding_->mouseTracker().forget(*this); }

std::string RenderPointerRegion::describe() const { return hovered_ ? "hovered" : std::string(); }

void RenderPointerRegion::setBehavior(HitTestBehavior behavior) {
  if (behavior == behavior_) return;
  behavior_ = behavior;
  // Neither layout nor paint changes, but what lies under the cursor may have.
  binding_->mouseTracker().invalidate();
}

void RenderPointerRegion::setCallbacks(const PointerCallbacks& callbacks) {
  const bool hoverChanged = wantsHover() != (callbacks.onEnter || callbacks.onExit);
  onEnter_ = callbacks.onEnter;
  onExit_ = callbacks.onExit;
  tap_.onTapDown = callbacks.onTapDown;
  tap_.onTap = callbacks.onTap;
  tap_.onTapCancel = callbacks.onTapCancel;
  if (hoverChanged) binding_->mouseTracker().invalidate();
}

bool RenderPointerRegion::hitTest(HitTestResult& result, Offset position) {
  if (!hasSize() || !paintBounds().contains(position)) return false;
  const bool child = hitTestChildren(result, position);
  if (child || behavior_ != HitTestBehavior::DeferToChild) result.add(this, position);
  return child || behavior_ == HitTestBehavior::Opaque;
}

void RenderPointerRegion::receiveDown(const PointerEvent& down) {
  if (tap_.isWanted()) tap_.addPointer(down);
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
