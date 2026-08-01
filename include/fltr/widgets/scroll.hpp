#pragma once

#include <memory>

#include "fltr/animation/driver.hpp"
#include "fltr/animation/tween.hpp"
#include "fltr/scroll/viewport.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/inherited.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// ScrollScope -- the position, ambient to everything inside the scrollable
// ---------------------------------------------------------------------------

/// Publishes the enclosing scroll position to its subtree.
///
/// This is the whole of the "no new dispatch mechanisms" principle in practice:
/// a sticky header, a focus traversal wanting `ensureVisible`, or a consumer's
/// own code reads the position from here in O(1), rather than a notification
/// bubbling up a second tree. The cost, recorded: an *ancestor* cannot passively
/// observe a descendant scrolling, so anything drawn outside the scrollable --
/// the scrollbar and the overscroll stretch below -- is handed a controller
/// instead.
class ScrollScope final : public Configure<ScrollScope, InheritedWidget> {
public:
  struct Args {
    Key key;
    ScrollPosition* position = nullptr;
    WidgetRef child;
  };

  explicit ScrollScope(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "ScrollScope"; }
  WidgetRef child() const noexcept { return args_.child; }
  ScrollPosition* position() const noexcept { return args_.position; }

  bool updateShouldNotify(const ScrollScope& previous) const noexcept {
    return args_.position != previous.args_.position;
  }

  /// Null when there is no scrollable above, which is what lets a widget work
  /// both inside one and outside.
  static ScrollPosition* of(BuildContext& context) {
    InheritedElementBase* node = context.element().dependOnInherited(widgetTypeOf<ScrollScope>());
    return node == nullptr ? nullptr
                           : static_cast<InheritedElement<ScrollScope>*>(node)->config().position();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

class Viewport final : public Configure<Viewport, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    Axis axis = Axis::Vertical;
    ScrollPosition* position = nullptr;
    WidgetRef child;
  };
  using Render = RenderViewport;

  explicit Viewport(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Viewport"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderViewport> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderViewport>(args_.position, args_.axis);
  }
  void updateRenderObject(BuildContext&, RenderViewport& render) const {
    render.setPosition(args_.position);
    render.setAxis(args_.axis);
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Scrollable
// ---------------------------------------------------------------------------

class Scrollable;

class ScrollableState final : public State<Scrollable> {
public:
  void initState() override;
  void didUpdateWidget(const Scrollable& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

  ScrollPosition& position() noexcept { return position_; }

private:
  void adoptConfiguration();

  ScrollPosition position_;
};

/// A scroll view over a single child.
///
/// The child is laid out once, with the scroll axis unbounded, and painted at a
/// negative offset -- the same choice Flutter made for `SingleChildScrollView`.
/// The accepted cost, stated plainly: every child is built and laid out whether
/// or not it is visible, so a few hundred are fine and a few thousand are not.
/// Nothing about the position, the physics, the wheel or the scrollbar would
/// change if lazy children arrived later.
class Scrollable final : public Configure<Scrollable, StatefulWidget> {
public:
  struct Args {
    Key key;
    Axis axis = Axis::Vertical;
    /// Consumer-owned, and the only way to reach this scroll view from outside
    /// it. Never owned here.
    ScrollController* controller = nullptr;
    /// Also consumer-owned, and stateless: null takes the framework default.
    const ScrollPhysics* physics = nullptr;
    WidgetRef child;
  };

  explicit Scrollable(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Scrollable"; }
  Axis axis() const noexcept { return args_.axis; }
  ScrollController* controller() const noexcept { return args_.controller; }
  const ScrollPhysics* physics() const noexcept { return args_.physics; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Scrollable>> createState() const {
    return std::make_unique<ScrollableState>();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Watching a scrollable from outside it
// ---------------------------------------------------------------------------

/// The half every indicator drawn outside a `Scrollable` shares: the position
/// its controller currently drives, and a subscription to each.
///
/// The controller is watched as well as the position because it has none until
/// the scrollable below has built -- an indicator wrapping a scrollable is
/// always built first, and has to tolerate that rather than assume it away.
template <class W>
class ScrollObserverState : public State<W> {
public:
  void dispose() override {
    controllerSubscription_.detach();
    positionSubscription_.detach();
  }

protected:
  ScrollPosition* position() const noexcept { return position_; }

  void bindController(ScrollController* controller) {
    if (controller != controller_) {
      controller_ = controller;
      if (controller_) {
        subscribeMember<ScrollObserverState, &ScrollObserverState::syncPosition>(
            *controller_, controllerSubscription_, this);
      } else {
        controllerSubscription_.detach();
      }
    }
    syncPosition();
  }

  /// The offset, the extents or the overscroll moved.
  virtual void didScroll() {}
  /// The controller found or lost its scrollable.
  virtual void didAttach() {}

private:
  void syncPosition() {
    ScrollPosition* next = controller_ ? controller_->positionOrNull() : nullptr;
    if (next == position_) return;
    position_ = next;
    if (position_) {
      subscribeMember<ScrollObserverState, &ScrollObserverState::didScroll>(
          *position_, positionSubscription_, this);
    } else {
      positionSubscription_.detach();
    }
    didAttach();
  }

  ScrollController* controller_ = nullptr;
  ScrollPosition* position_ = nullptr;
  Subscription controllerSubscription_;
  Subscription positionSubscription_;
};

// ---------------------------------------------------------------------------
// Scrollbar
// ---------------------------------------------------------------------------

class ScrollbarThumb final : public Configure<ScrollbarThumb, LeafRenderObjectWidget> {
public:
  struct Args {
    Key key;
    ScrollPosition* position = nullptr;
    Axis axis = Axis::Vertical;
    float minExtent = 24.0f;
    Color color{255, 255, 255, 120};
    BorderRadius radius = BorderRadius::zero();
    ValueListenable<float>* opacity = nullptr;
  };
  using Render = RenderScrollbarThumb;

  explicit ScrollbarThumb(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "ScrollbarThumb"; }

  std::unique_ptr<RenderScrollbarThumb> createRenderObject(BuildContext&) const {
    auto thumb = std::make_unique<RenderScrollbarThumb>(args_.position, args_.axis);
    configure(*thumb);
    return thumb;
  }
  void updateRenderObject(BuildContext&, RenderScrollbarThumb& render) const {
    render.setPosition(args_.position);
    render.setAxis(args_.axis);
    configure(render);
  }

private:
  void configure(RenderScrollbarThumb& render) const {
    render.setMinExtent(args_.minExtent);
    render.setColor(args_.color);
    render.setRadius(args_.radius);
    render.setOpacity(args_.opacity);
  }

  Args args_;
};

class Scrollbar;

class ScrollbarState final : public ScrollObserverState<Scrollbar> {
public:
  void initState() override;
  void didUpdateWidget(const Scrollbar& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

protected:
  void didScroll() override;
  void didAttach() override;

private:
  WidgetRef buildTrack();
  ScrollbarGeometry geometry() const;
  /// A press on the track outside the thumb is not a grab, so the offset does
  /// not leap to wherever the pointer happened to land.
  void grabAt(Offset local);
  void dragThumb(float delta);
  void show();
  void countIdle(float seconds);
  void setExpanded(bool hovered, bool dragging);

  AnimationDriver fade_{{.duration = 0.2f, .curve = Curves::easeOut}};
  AnimatedValue<float> visibility_{fade_, {0.0f, 1.0f}};
  Ticker idle_{[](void* self, float seconds) {
                 static_cast<ScrollbarState*>(self)->countIdle(seconds);
               },
               this};
  float still_ = 0.0f;
  bool hovered_ = false;
  bool dragging_ = false;
  bool grabbed_ = false;
};

/// A thumb over a scroll view: draggable, fading out when nothing has happened,
/// widening while the pointer is on it. Behaviour and geometry only -- the
/// colour, thickness and corner radius are the consumer's.
///
/// It wraps the `Scrollable` rather than living inside it, so it is told which
/// scroll view it belongs to by the controller they share. Wrapping is what
/// makes its track exactly the viewport's own extent, which is the assumption
/// the thumb geometry is built on.
class Scrollbar final : public Configure<Scrollbar, StatefulWidget> {
public:
  struct Args {
    Key key;
    ScrollController* controller = nullptr;
    float thickness = 6.0f;
    float hoveredThickness = 10.0f;
    float minThumbExtent = 24.0f;
    Color color{255, 255, 255, 120};
    BorderRadius radius = BorderRadius::all(3.0f);
    /// Seconds of stillness before the thumb fades out. Zero keeps it up.
    float fadeDelay = 0.7f;
    WidgetRef child;
  };

  explicit Scrollbar(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Scrollbar"; }
  const Args& args() const noexcept { return args_; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Scrollbar>> createState() const {
    return std::make_unique<ScrollbarState>();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Overscroll
// ---------------------------------------------------------------------------

class Overscroll;

class OverscrollState final : public ScrollObserverState<Overscroll> {
public:
  void initState() override;
  void didUpdateWidget(const Overscroll& previous) override;
  WidgetRef build(BuildContext& context) override;

protected:
  void didScroll() override;

private:
  Observable<Transform2D> stretch_{Transform2D::identity()};
  Alignment pivot_ = Alignment::topCenter();
};

/// Shows what the boundary refused, by scaling the scroll view away from the
/// edge being pushed.
///
/// It is the render-attached path and nothing else: the scale is an observable
/// the `Transform` render object subscribes to, so a frame of stretching
/// repaints one object and rebuilds nothing. Only the pivot lives in the widget,
/// so a rebuild happens once per overscroll episode rather than once per frame.
///
/// This is the visual for physics that clamp. Physics that let the offset leave
/// its range bounce instead and never overscroll in this sense, so the two
/// treatments cannot appear at once.
class Overscroll final : public Configure<Overscroll, StatefulWidget> {
public:
  struct Args {
    Key key;
    ScrollController* controller = nullptr;
    /// How much of the viewport a fully saturated pull adds.
    float maxStretch = 0.15f;
    WidgetRef child;
  };

  explicit Overscroll(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Overscroll"; }
  ScrollController* controller() const noexcept { return args_.controller; }
  float maxStretch() const noexcept { return args_.maxStretch; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Overscroll>> createState() const {
    return std::make_unique<OverscrollState>();
  }

private:
  Args args_;
};

}  // namespace fltr
