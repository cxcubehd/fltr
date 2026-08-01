#pragma once

#include <cstdint>
#include <memory>

#include "fltr/animation/curves.hpp"
#include "fltr/animation/ticker.hpp"
#include "fltr/scroll/physics.hpp"

namespace fltr {

class RenderObject;
class RenderViewport;
class ScrollController;

/// What is currently moving the offset, and therefore what the next frame does
/// with it.
///
/// DIVERGENCE: Flutter models these as a class hierarchy a consumer can extend.
/// This is a closed set of five, so it is one value with a tag and the state
/// each mode needs. A drag or a wheel notch therefore starts without allocating,
/// which a hierarchy could not offer; the cost is that adding a sixth mode edits
/// a switch, and a consumer cannot supply an activity of its own.
enum class ScrollActivityKind : std::uint8_t {
  Idle,
  /// A pointer is holding the offset. Nothing else may move it.
  Drag,
  /// Coasting under a simulation, which is where a fling and a spring-back live.
  Ballistic,
  /// Easing toward an offset the wheel keeps adding to.
  Wheel,
  /// Running a fixed-duration curve, which is what animateTo and ensureVisible
  /// produce.
  Driven,
};

/// Where a scroll view is, what it may become, and what is moving it.
///
/// One dimension only: the axis lives on it for the benefit of whoever draws or
/// hit-tests it, but nothing here does anything two-dimensional. It is a
/// `Listenable` that fires whenever the offset, the extents, or the overscroll
/// changes -- so a viewport observing it for paint scrolls without laying out,
/// and a `Watch` over it rebuilds only when a consumer asked for that.
class ScrollPosition final : public Listenable {
public:
  ScrollPosition() = default;
  ~ScrollPosition() override;

  /// A position ticks only once it has a registry, which is what a State does
  /// from `initState`. Until then a fling simply does not start.
  void attach(TickerRegistry& tickers) { ticker_.attach(tickers); }
  void detach() { ticker_.detach(); }

  const ScrollMetrics& metrics() const noexcept { return metrics_; }
  float pixels() const noexcept { return metrics_.pixels; }
  float viewportDimension() const noexcept { return metrics_.viewportDimension; }
  float maxScrollExtent() const noexcept { return metrics_.maxScrollExtent; }

  /// What the boundary refused, signed outward, decaying once nothing is
  /// pushing. This is what an overscroll indicator shows. It stays zero under
  /// physics that let the offset leave its range instead, which is why the
  /// bounce and the stretch never appear at once.
  float overscroll() const noexcept { return overscroll_; }

  Axis axis() const noexcept { return axis_; }
  void setAxis(Axis axis) noexcept { axis_ = axis; }

  /// Null once nothing outside is holding this position -- including when a
  /// consumer's controller was destroyed before the tree that used it.
  ScrollController* controller() const noexcept { return controller_; }

  ScrollActivityKind activity() const noexcept { return activity_.kind; }
  bool isScrolling() const noexcept { return activity_.kind != ScrollActivityKind::Idle; }

  void setPhysics(const ScrollPhysics* physics);
  const ScrollPhysics& physics() const noexcept { return *physics_; }

  // --- what the viewport reports during its layout -------------------------

  void applyViewportDimension(float value);
  void applyContentDimensions(float minExtent, float maxExtent);

  /// Sets the offset without consulting the boundary, for the one case that has
  /// no boundary yet: restoring where a scrollable was before its first layout
  /// has told this position how far its content reaches.
  void correctPixels(float value);

  // --- what a controller or a consumer asks for ----------------------------

  void jumpTo(float value);
  void animateTo(float value, float duration, Curve curve = Curves::easeInOut);
  /// Scrolls until `target` is inside the viewport. `alignment` is where it
  /// lands: 0 against the leading edge, 1 against the trailing one. Does nothing
  /// if the target is not below this position's viewport.
  void ensureVisible(const RenderObject& target, float alignment = 0.0f, float duration = 0.0f);

  // --- what the Scrollable's gestures drive --------------------------------

  void beginDrag();
  /// `delta` is the pointer's own movement, so the offset moves the other way.
  void applyDragDelta(float delta);
  /// `velocity` is likewise the pointer's, and the fling goes the other way.
  void endDrag(float velocity);

  /// A wheel notch, eased toward rather than applied: repeated notches
  /// accumulate into one target instead of jumping once each.
  bool applyWheelDelta(float delta);
  /// A trackpad pan, which is already a displacement and is applied as one.
  bool applyPanDelta(float delta);

  void goBallistic(float velocity);
  void goIdle();

private:
  friend class RenderViewport;
  friend class ScrollController;

  struct Activity {
    ScrollActivityKind kind = ScrollActivityKind::Idle;
    std::unique_ptr<Simulation> simulation;
    float elapsed = 0.0f;
    /// Where a wheel or a driven activity is heading, and where the latter
    /// started from.
    float target = 0.0f;
    float origin = 0.0f;
    float duration = 0.0f;
    Curve curve;
  };

  /// Moves the offset as far as the boundary allows, and reports what it
  /// refused -- which is how a ballistic activity learns it has arrived.
  float setPixels(float value);
  void didOverscroll(float amount);
  void relaxOverscroll(float seconds);
  void beginActivity(ScrollActivityKind kind);
  void tick(float seconds);
  void syncTicker();

  Ticker ticker_{[](void* self, float seconds) { static_cast<ScrollPosition*>(self)->tick(seconds); },
                 this};
  ScrollMetrics metrics_;
  Activity activity_;
  const ScrollPhysics* physics_ = &defaultScrollPhysics();
  RenderViewport* viewport_ = nullptr;
  ScrollController* controller_ = nullptr;
  float overscroll_ = 0.0f;
  Axis axis_ = Axis::Vertical;
};

class ScrollableState;

/// A handle on a scroll view from outside it.
///
/// Consumer-owned and referred to by pointer, never owned by a widget. It holds
/// no offset of its own: the `Scrollable` owns the position and lends it here.
/// The two know each other and each clears the link on the way out, so either
/// one outliving the other is inert rather than dangling -- which matters,
/// because a consumer's controller and the tree that uses it are destroyed in
/// whatever order their scopes end.
///
/// DIVERGENCE: Flutter's controller drives any number of attached positions.
/// This one drives at most one, because the case that needs more -- one bar
/// controlling several lists -- is not one this framework has, and one position
/// makes `offset()` an answer rather than a choice.
class ScrollController final : public Listenable {
public:
  explicit ScrollController(float initialOffset = 0.0f) noexcept : initialOffset_(initialOffset) {}
  ~ScrollController() override;

  /// Whether a scrollable is currently using this controller. False before the
  /// scrollable's first build, so anything drawing from a controller has to
  /// tolerate it -- which is why this is a `Listenable`: attaching notifies.
  bool hasClient() const noexcept { return position_ != nullptr; }
  ScrollPosition& position() const;
  ScrollPosition* positionOrNull() const noexcept { return position_; }

  float offset() const noexcept { return position_ ? position_->pixels() : initialOffset_; }
  float initialOffset() const noexcept { return initialOffset_; }

  void jumpTo(float value);
  void animateTo(float value, float duration, Curve curve = Curves::easeInOut);

private:
  friend class ScrollPosition;
  friend class ScrollableState;

  void attach(ScrollPosition& position);
  void detach();

  ScrollPosition* position_ = nullptr;
  float initialOffset_;
};

}  // namespace fltr
