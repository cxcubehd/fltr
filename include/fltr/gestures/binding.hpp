#pragma once

#include <vector>

#include "fltr/core/config.hpp"
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
/// terms of gestures, hover and signals, so nothing wants that today.
class PointerBinding {
public:
  PointerBinding() = default;

  PointerBinding(const PointerBinding&) = delete;
  PointerBinding& operator=(const PointerBinding&) = delete;

  /// One pointer event, tested against `root`. Called by the consumer at
  /// whatever rate its input system produces events; unrelated to the frame.
  void dispatch(const PointerEvent& event, RenderBox& root);

  /// A wheel notch or a trackpad pan. Offered to each region under the point,
  /// innermost first, until one consumes it; returns whether one did.
  ///
  /// DIVERGENCE from every other pointer event: a signal never enters the arena.
  /// There is nothing to contest -- the pointer is not pressed and no gesture is
  /// forming -- so the innermost scrollable that can still move takes it and the
  /// rest see it only if that one declines. This is what a browser does when a
  /// scrolled-to-the-end list lets the page scroll instead.
  bool dispatchSignal(const PointerSignalEvent& event, RenderBox& root);

  /// Re-resolves hover when the tree may have moved beneath the cursor. Runs
  /// once per frame and does nothing at all when nothing moved.
  void settleHover(RenderBox& root);

  // --- the gesture clock --------------------------------------------------

  /// Time enters here the way it enters the ticker: from the consumer's frame,
  /// never from a clock this layer reads. That is what makes a long press fire
  /// while the pointer is perfectly still, and it is what a test advances to
  /// make a timeout land without waiting for one.
  void advanceTime(float seconds);
  float now() const noexcept { return now_; }
  bool hasPendingTimeouts() const noexcept { return !timeouts_.empty(); }

  void scheduleTimeout(GestureRecognizer& recognizer, PointerId pointer, float delay);
  void cancelTimeouts(GestureRecognizer& recognizer);

  GestureArena& arena() noexcept { return arena_; }
  MouseTracker& mouseTracker() noexcept { return tracker_; }
  const MouseTracker& mouseTracker() const noexcept { return tracker_; }

  /// What the cursor should look like where it currently is.
  MouseCursor cursor() const noexcept { return tracker_.cursor(); }

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

  struct Timeout {
    float deadline;
    PointerId pointer;
    GestureRecognizer* recognizer;
  };

  /// Every entry point records into `path_` and walks it, so none may run inside
  /// another -- a nested hit test would reallocate the list the outer walk is
  /// holding. A game callback dispatching an event or driving a frame is the way
  /// in. Restored however the scope is left, as the pipeline's phases are, so a
  /// checked build reports the re-entry rather than a second failure during
  /// unwinding.
  class DispatchScope {
  public:
    explicit DispatchScope(PointerBinding& owner) : owner_(owner) {
      FLTR_EXPECTS(!owner_.dispatching_, "a pointer callback re-entered pointer dispatch");
      owner_.dispatching_ = true;
    }
    ~DispatchScope() { owner_.dispatching_ = false; }

    DispatchScope(const DispatchScope&) = delete;
    DispatchScope& operator=(const DispatchScope&) = delete;

  private:
    PointerBinding& owner_;
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
  std::vector<Timeout> timeouts_;
  /// The deadlines being fired, held for the same reason `routing_` is.
  std::vector<Timeout> firing_;
  float now_ = 0.0f;
  int hitTests_ = 0;
  bool dispatching_ = false;
};

}  // namespace fltr
