#pragma once

#include <span>
#include <vector>

#include "fltr/core/function_ref.hpp"
#include "fltr/core/geometry.hpp"

namespace fltr {

struct PointerEvent;
class HitTestResult;

/// Anything that can receive a routed pointer event. Render objects that take
/// input implement this.
class HitTestTarget {
public:
  virtual ~HitTestTarget() = default;
  virtual void handleEvent(const PointerEvent& event, Offset localPosition) = 0;
  /// Only for dump output.
  virtual const char* targetName() const { return "HitTestTarget"; }
};

struct HitTestEntry {
  HitTestTarget* target = nullptr;
  /// The event position in this target's own coordinate space.
  Offset localPosition;
};

/// The ordered set of targets under a point, deepest (topmost) first.
///
/// DIVERGENCE: Flutter records a full global->local transform per entry so a
/// consumer can later re-derive any space. We record the already-resolved local
/// position instead. That is everything routing, tap, and hover need, and it
/// keeps the result trivially copyable. The cost is that a consumer cannot
/// re-project an entry into a different space after the fact.
class HitTestResult {
public:
  void add(HitTestTarget* target, Offset localPosition) {
    path_.push_back({target, localPosition});
  }

  /// Runs `hitTest` in a space translated by `offset`, i.e. for a child painted
  /// at `offset` within this object.
  bool addWithPaintOffset(Offset offset, Offset position,
                          FunctionRef<bool(HitTestResult&, Offset)> hitTest) {
    return hitTest(*this, position - offset);
  }

  /// Runs `hitTest` in a space related to this one by `transform`. Returns false
  /// without recursing if the transform is not invertible.
  bool addWithPaintTransform(const Transform2D& transform, Offset position,
                             FunctionRef<bool(HitTestResult&, Offset)> hitTest) {
    if (transform.isIdentity()) return hitTest(*this, position);
    Transform2D inverse;
    if (!transform.invert(inverse)) return false;
    return hitTest(*this, inverse.apply(position));
  }

  std::span<const HitTestEntry> path() const noexcept { return path_; }
  bool empty() const noexcept { return path_.empty(); }
  void clear() noexcept { path_.clear(); }

private:
  std::vector<HitTestEntry> path_;
};

}  // namespace fltr
