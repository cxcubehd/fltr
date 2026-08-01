#pragma once

#include <cstddef>
#include <vector>

#include "fltr/core/geometry.hpp"
#include "fltr/gestures/events.hpp"
#include "fltr/render/hit_test.hpp"

namespace fltr {

class RenderPointerRegion;

/// Enter and exit, synthesized by diffing hit-test results, and the cursor that
/// follows from what lies under the pointer.
///
/// This is deliberately not gesture recognition and never touches the arena:
/// nothing competes for a hover, and there is nothing to win. What it needs
/// instead is to re-resolve what lies under the cursor whenever that could have
/// changed -- when the cursor moves, and when the tree moves beneath a
/// stationary cursor -- and to diff the answer against the previous one.
///
/// It follows one cursor. A game has one mouse, and a touch pointer never
/// hovers.
class MouseTracker {
public:
  /// Adopts a fresh hit test at `position` as the current answer, firing exit
  /// for regions that dropped out and enter for regions that appeared.
  void update(Offset position, const HitTestResult& path);

  /// The tree may have moved beneath the cursor, so the current answer can no
  /// longer be trusted.
  void invalidate() noexcept { stale_ = true; }
  bool needsResolve() const noexcept { return stale_ && hasCursor_; }

  /// The cursor left the surface: everything hovered is exited.
  void clear();

  /// A region being destroyed. Dropped without an exit callback: the state that
  /// callback would have updated is going away with it.
  void forget(RenderPointerRegion& region);

  bool hasCursor() const noexcept { return hasCursor_; }
  Offset position() const noexcept { return position_; }
  std::size_t hoveredCount() const noexcept { return hovered_.size(); }

  /// The innermost region under the pointer that has an opinion. `Basic` when
  /// none has, so the consumer always has something to apply.
  MouseCursor cursor() const noexcept { return cursor_; }

private:
  void resolveCursor() noexcept;

  std::vector<RenderPointerRegion*> hovered_;
  /// Holds the incoming set while it is built, then the outgoing one while exit
  /// callbacks run. A member, so a cursor move in the steady state allocates
  /// nothing.
  std::vector<RenderPointerRegion*> scratch_;
  Offset position_;
  MouseCursor cursor_ = MouseCursor::Basic;
  bool hasCursor_ = false;
  bool stale_ = false;
};

}  // namespace fltr
