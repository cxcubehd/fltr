#include "ui/drag.hpp"

#include "ui/scroll.hpp"

namespace fltrdemo {

RenderDragTarget::~RenderDragTarget() { router_->forget(*this); }

void PointerRouter::forget(RenderDragTarget& target) noexcept {
  // A rebuild can destroy the target mid-drag -- the same hazard the framework
  // avoids by routing to recognizers it owns rather than to a cached path.
  if (target_ == &target) target_ = nullptr;
}

const fltr::HitTestResult& PointerRouter::hitTest(fltr::WidgetBinding& binding,
                                                  fltr::Offset position) {
  ++hitTests_;
  path_.clear();
  if (fltr::RenderView* view = binding.renderView()) view->hitTest(path_, position);
  return path_;
}

void PointerRouter::down(fltr::WidgetBinding& binding, fltr::Offset position) {
  target_ = nullptr;
  // Deepest first, so the innermost draggable under the press wins -- the same
  // order the gesture arena awards an unresolved sweep in.
  for (const fltr::HitTestEntry& entry : hitTest(binding, position).path()) {
    if (auto* target = dynamic_cast<RenderDragTarget*>(entry.target)) {
      target_ = target;
      localAtDown_ = entry.localPosition;
      globalAtDown_ = position;
      lastGlobal_ = position;
      if (target->callbacks().onDragStart) {
        target->callbacks().onDragStart({localAtDown_, fltr::Offset::zero(), target->size()});
      }
      return;
    }
  }
}

void PointerRouter::move(fltr::Offset position) {
  if (!target_) return;
  const fltr::Offset delta = position - lastGlobal_;
  lastGlobal_ = position;
  if (!target_->callbacks().onDragUpdate) return;
  // The target has not moved, so its local space differs from the view's by a
  // constant: the position at the press plus everything the pointer has
  // travelled since. That keeps working after the pointer leaves the control,
  // which is the whole reason a drag needs more than hover.
  const fltr::Offset local = localAtDown_ + (position - globalAtDown_);
  target_->callbacks().onDragUpdate({local, delta, target_->size()});
}

void PointerRouter::up() {
  if (!target_) return;
  RenderDragTarget* target = target_;
  target_ = nullptr;
  if (target->callbacks().onDragEnd) target->callbacks().onDragEnd();
}

void PointerRouter::cancel() { target_ = nullptr; }

void PointerRouter::wheel(fltr::WidgetBinding& binding, fltr::Offset position, float ticks,
                          float lineHeight) {
  if (ticks == 0.0f) return;
  for (const fltr::HitTestEntry& entry : hitTest(binding, position).path()) {
    if (auto* viewport = dynamic_cast<RenderScrollViewport*>(entry.target)) {
      viewport->controller().scrollBy(-ticks * lineHeight);
      return;
    }
  }
}

}  // namespace fltrdemo
