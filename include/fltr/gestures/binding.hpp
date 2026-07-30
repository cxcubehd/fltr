#pragma once

#include <vector>

#include "fltr/gestures/arena.hpp"
#include "fltr/gestures/events.hpp"
#include "fltr/gestures/mouse_tracker.hpp"
#include "fltr/render/hit_test.hpp"

namespace fltr {

class GestureRecognizer;
class RenderBox;

/// Where the consumer pushes pointer events, and the one place that decides what
/// each of them implies.
///
/// DIVERGENCE: Flutter caches the hit-test path per pointer at down and
/// dispatches every later event of that gesture along it. We hit test only for
/// the down event and route everything after it by pointer id, to the
/// recognizers that claimed it. Two reasons:
///
///   - a cached path is a list of raw render-object pointers held across frames,
///     and a rebuild can destroy one mid-gesture; a route is owned by the
///     recognizer that registered it and is withdrawn when the recognizer dies;
///   - a recognizer must keep receiving events after the pointer leaves the
///     region it started in, which is what routing by pointer gives directly.
///
/// The cost is that a render object cannot receive raw move or up events without
/// going through a recognizer. The catalogue's interaction widget is defined in
/// terms of gestures and hover, so nothing wants that today.
class PointerBinding {
public:
  /// One pointer event, tested against `root`. Called by the consumer at
  /// whatever rate its input system produces events; unrelated to the frame.
  void dispatch(const PointerEvent& event, RenderBox& root);

  /// Re-resolves hover when the tree may have moved beneath the cursor. Runs
  /// once per frame and does nothing at all when nothing moved.
  void settleHover(RenderBox& root);

  GestureArena& arena() noexcept { return arena_; }
  MouseTracker& mouseTracker() noexcept { return tracker_; }
  const MouseTracker& mouseTracker() const noexcept { return tracker_; }

  void addRoute(PointerId pointer, GestureRecognizer& recognizer);
  void removeRoute(PointerId pointer, GestureRecognizer& recognizer);
  void removeRoutes(GestureRecognizer& recognizer);

  /// How many hit tests have been run. A frame that changes nothing must run
  /// none, which is worth measuring rather than asserting.
  int hitTestCount() const noexcept { return hitTests_; }

private:
  struct Route {
    PointerId pointer;
    GestureRecognizer* recognizer;
  };

  const HitTestResult& hitTest(RenderBox& root, Offset position);
  void routeToRecognizers(const PointerEvent& event);

  GestureArena arena_;
  MouseTracker tracker_;
  /// Reused across events, so dispatching in the steady state allocates nothing.
  HitTestResult path_;
  std::vector<Route> routes_;
  /// The routes of the event being delivered. A recognizer can withdraw itself
  /// or another from its own callback, so `removeRoute` blanks entries here
  /// rather than erasing them, keeping the walk's indices valid.
  std::vector<Route> routing_;
  int hitTests_ = 0;
};

}  // namespace fltr
