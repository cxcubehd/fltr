#include "fltr/scroll/position.hpp"

#include "fltr/scroll/viewport.hpp"

namespace fltr {

namespace {

/// How quickly the offset closes on a wheel target. Expressed as a time
/// constant rather than a duration because new notches keep arriving mid-flight
/// and an exponential approach retargets without restarting -- the same
/// requirement `AnimationDriver` already met for a different quantity.
constexpr float kWheelEaseTau = 0.04f;
/// The same, for letting a refused overscroll go once nothing is pushing it.
constexpr float kOverscrollReleaseTau = 0.06f;

}  // namespace

ScrollPosition::~ScrollPosition() {
  if (viewport_) viewport_->forgetPosition();
  if (controller_) controller_->position_ = nullptr;
}

void ScrollPosition::setPhysics(const ScrollPhysics* physics) {
  physics_ = physics ? physics : &defaultScrollPhysics();
}

// ---------------------------------------------------------------------------
// Moving the offset
// ---------------------------------------------------------------------------

float ScrollPosition::setPixels(float value) {
  const float refused = physics_->applyBoundaryConditions(metrics_, value);
  const float next = value - refused;
  if (next != metrics_.pixels) {
    metrics_.pixels = next;
    notifyListeners();
  }
  if (refused != 0.0f) didOverscroll(refused);
  return refused;
}

void ScrollPosition::didOverscroll(float amount) {
  // Saturating rather than linear, so leaning on a boundary approaches a limit
  // instead of winding up an arbitrarily large stretch.
  const float limit = std::max(1.0f, metrics_.viewportDimension);
  const float slack = std::max(0.0f, 1.0f - std::fabs(overscroll_) / limit);
  const float next = std::clamp(overscroll_ + amount * slack, -limit, limit);
  if (next == overscroll_) return;
  overscroll_ = next;
  syncTicker();
  notifyListeners();
}

void ScrollPosition::relaxOverscroll(float seconds) {
  if (overscroll_ == 0.0f) return;
  overscroll_ *= std::exp(-seconds / kOverscrollReleaseTau);
  if (std::fabs(overscroll_) < physics_->tolerance.distance) overscroll_ = 0.0f;
  notifyListeners();
}

// ---------------------------------------------------------------------------
// Dimensions, reported by the viewport during its layout
// ---------------------------------------------------------------------------

void ScrollPosition::applyViewportDimension(float value) {
  if (value == metrics_.viewportDimension) return;
  metrics_.viewportDimension = value;
  notifyListeners();
}

void ScrollPosition::applyContentDimensions(float minExtent, float maxExtent) {
  if (minExtent == metrics_.minScrollExtent && maxExtent == metrics_.maxScrollExtent) return;
  metrics_.minScrollExtent = minExtent;
  metrics_.maxScrollExtent = maxExtent;
  notifyListeners();
  // Content that shrank under a scrolled-down view leaves the offset outside a
  // range it was inside a moment ago. Settling it is a ballistic, not a clamp,
  // so it springs back rather than jumping -- and never a second layout pass.
  if (activity_.kind == ScrollActivityKind::Idle) goBallistic(0.0f);
}

void ScrollPosition::correctPixels(float value) {
  if (value == metrics_.pixels) return;
  metrics_.pixels = value;
  notifyListeners();
}

// ---------------------------------------------------------------------------
// Activities
// ---------------------------------------------------------------------------

void ScrollPosition::beginActivity(ScrollActivityKind kind) {
  activity_.simulation.reset();
  activity_.kind = kind;
  activity_.elapsed = 0.0f;
  syncTicker();
}

void ScrollPosition::goIdle() { beginActivity(ScrollActivityKind::Idle); }

void ScrollPosition::goBallistic(float velocity) {
  std::unique_ptr<Simulation> simulation =
      physics_->createBallisticSimulation(metrics_, velocity);
  if (!simulation) {
    goIdle();
    return;
  }
  beginActivity(ScrollActivityKind::Ballistic);
  activity_.simulation = std::move(simulation);
  syncTicker();
}

void ScrollPosition::syncTicker() {
  const bool needsTime = activity_.kind != ScrollActivityKind::Idle || overscroll_ != 0.0f;
  if (needsTime) {
    ticker_.start();
  } else {
    ticker_.stop();
  }
}

void ScrollPosition::tick(float seconds) {
  switch (activity_.kind) {
    case ScrollActivityKind::Idle:
    case ScrollActivityKind::Drag:
      break;

    case ScrollActivityKind::Ballistic: {
      activity_.elapsed += seconds;
      const bool arrived = setPixels(activity_.simulation->x(activity_.elapsed)) != 0.0f;
      if (arrived || activity_.simulation->isDone(activity_.elapsed)) goIdle();
      break;
    }

    case ScrollActivityKind::Wheel: {
      const float remaining = activity_.target - metrics_.pixels;
      if (std::fabs(remaining) < physics_->tolerance.distance) {
        setPixels(activity_.target);
        goIdle();
        break;
      }
      setPixels(metrics_.pixels + remaining * (1.0f - std::exp(-seconds / kWheelEaseTau)));
      break;
    }

    case ScrollActivityKind::Driven: {
      activity_.elapsed += seconds;
      const float t = activity_.duration > 0.0f
                          ? std::min(1.0f, activity_.elapsed / activity_.duration)
                          : 1.0f;
      setPixels(lerpF(activity_.origin, activity_.target, activity_.curve(t)));
      if (t >= 1.0f) goIdle();
      break;
    }
  }

  // A finger resting past the edge holds the stretch open; everything else
  // lets it go.
  if (activity_.kind != ScrollActivityKind::Drag) relaxOverscroll(seconds);
  syncTicker();
}

// ---------------------------------------------------------------------------
// What a controller asks for
// ---------------------------------------------------------------------------

void ScrollPosition::jumpTo(float value) {
  goIdle();
  setPixels(metrics_.clampToRange(value));
}

void ScrollPosition::animateTo(float value, float duration, Curve curve) {
  const float target = metrics_.clampToRange(value);
  if (duration <= 0.0f || target == metrics_.pixels) {
    jumpTo(target);
    return;
  }
  beginActivity(ScrollActivityKind::Driven);
  activity_.origin = metrics_.pixels;
  activity_.target = target;
  activity_.duration = duration;
  activity_.curve = curve;
}

void ScrollPosition::ensureVisible(const RenderObject& target, float alignment, float duration) {
  if (!viewport_) return;
  reveal(metrics_.clampToRange(viewport_->offsetToReveal(target, alignment)), duration);
}

void ScrollPosition::revealMinimally(const RenderObject& target, float duration) {
  if (!viewport_) return;
  reveal(metrics_.clampToRange(viewport_->offsetToRevealMinimally(target)), duration);
}

void ScrollPosition::reveal(float value, float duration) {
  if (value == metrics_.pixels) return;
  if (duration <= 0.0f) {
    jumpTo(value);
  } else {
    animateTo(value, duration);
  }
}

// ---------------------------------------------------------------------------
// What the Scrollable's gestures drive
// ---------------------------------------------------------------------------

void ScrollPosition::beginDrag() {
  if (!physics_->allowsUserScrolling(metrics_)) return;
  beginActivity(ScrollActivityKind::Drag);
}

void ScrollPosition::applyDragDelta(float delta) {
  if (activity_.kind != ScrollActivityKind::Drag) return;
  setPixels(metrics_.pixels - physics_->applyPhysicsToUserOffset(metrics_, delta));
}

void ScrollPosition::endDrag(float velocity) {
  if (activity_.kind != ScrollActivityKind::Drag) return;
  const float fling = -velocity;
  goBallistic(std::fabs(fling) < physics_->minFlingVelocity
                  ? 0.0f
                  : std::clamp(fling, -physics_->maxFlingVelocity, physics_->maxFlingVelocity));
}

bool ScrollPosition::applyWheelDelta(float delta) {
  if (activity_.kind == ScrollActivityKind::Drag || !physics_->allowsUserScrolling(metrics_)) {
    return false;
  }
  // A notch in flight adds to where we were already heading; one that arrives
  // after everything settled starts from where the offset actually is.
  const float from =
      activity_.kind == ScrollActivityKind::Wheel ? activity_.target : metrics_.pixels;
  const float target = metrics_.clampToRange(from + delta);
  if (target == metrics_.pixels && target == from) return false;
  beginActivity(ScrollActivityKind::Wheel);
  activity_.target = target;
  return true;
}

bool ScrollPosition::applyPanDelta(float delta) {
  if (activity_.kind == ScrollActivityKind::Drag || !physics_->allowsUserScrolling(metrics_)) {
    return false;
  }
  goIdle();
  const float before = metrics_.pixels;
  setPixels(metrics_.clampToRange(metrics_.pixels + delta));
  return metrics_.pixels != before;
}

// ---------------------------------------------------------------------------
// ScrollController
// ---------------------------------------------------------------------------

ScrollController::~ScrollController() {
  if (position_) position_->controller_ = nullptr;
}

ScrollPosition& ScrollController::position() const {
  FLTR_EXPECTS(position_ != nullptr, "this ScrollController is not attached to a Scrollable");
  return *position_;
}

void ScrollController::attach(ScrollPosition& position) {
  if (position_ == &position) return;
  detach();
  position_ = &position;
  position.controller_ = this;
  notifyListeners();
}

void ScrollController::detach() {
  if (!position_) return;
  position_->controller_ = nullptr;
  position_ = nullptr;
  notifyListeners();
}

void ScrollController::jumpTo(float value) {
  if (position_) position_->jumpTo(value);
}

void ScrollController::animateTo(float value, float duration, Curve curve) {
  if (position_) position_->animateTo(value, duration, curve);
}

}  // namespace fltr
