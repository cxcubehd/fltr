#pragma once

#include <utility>

#include "fltr/animation/ticker.hpp"
#include "fltr/gestures/binding.hpp"
#include "fltr/gestures/events.hpp"
#include "fltr/gestures/keyboard.hpp"
#include "fltr/render/object.hpp"
#include "fltr/widgets/basic.hpp"

namespace fltr {

/// What the game loop holds. Owns the three trees and runs the three phases --
/// build, layout, paint -- in that order, once per frame.
///
/// It owns neither the window nor the clock: the surface size is pushed in, and
/// a frame happens when the consumer asks for one.
class WidgetBinding {
public:
  WidgetBinding(Size surface, TextService& textService);
  ~WidgetBinding();

  WidgetBinding(const WidgetBinding&) = delete;
  WidgetBinding& operator=(const WidgetBinding&) = delete;

  /// Builds and mounts the tree. `buildRoot` runs inside a build scope, so it
  /// may create widgets; it is called exactly once.
  template <class BuildRoot>
  void attachRoot(BuildRoot&& buildRoot) {
    BuildScope scope(buildOwner_.arena());
    mountRoot(View::make({.child = std::forward<BuildRoot>(buildRoot)()}));
  }

  void setSurface(Size surface);
  Size surface() const noexcept { return surface_; }

  /// One pointer event, at whatever rate the consumer's input system produces
  /// them. Unrelated to the frame: an event is delivered when it arrives.
  void dispatchPointer(const PointerEvent& event);

  /// One wheel notch or trackpad pan. Returns whether a region consumed it, so
  /// the consumer can fall back to scrolling its own world when the UI declines.
  bool dispatchSignal(const PointerSignalEvent& event);

  /// One key event. Returns whether a handler consumed it, which is the same
  /// question: an unconsumed key belongs to the game.
  bool dispatchKey(const KeyEvent& event) { return keyboard_.dispatch(event); }

  /// One frame, given the time the consumer's loop measured since the last one.
  /// Advances animations, then builds what is dirty, then lays out and paints
  /// what that made dirty. A frame in which no time passed and nothing is dirty
  /// does no work in any phase and returns a Scene whose revision is unchanged.
  Scene drawFrame(float seconds = 0.0f);

  bool needsFrame() const noexcept {
    return buildOwner_.needsBuild() || pipeline_.needsFrame() ||
           pointers_.mouseTracker().needsResolve() || tickers_.hasActiveTickers() ||
           pointers_.hasPendingTimeouts();
  }

  /// What the cursor should look like where it currently is. Read once per frame
  /// and applied by the consumer, which is the only side that owns a window.
  MouseCursor cursor() const noexcept { return pointers_.cursor(); }

  BuildOwner& buildOwner() noexcept { return buildOwner_; }
  PipelineOwner& pipeline() noexcept { return pipeline_; }
  PointerBinding& pointers() noexcept { return pointers_; }
  KeyboardBinding& keyboard() noexcept { return keyboard_; }
  TickerRegistry& tickers() noexcept { return tickers_; }
  Element* rootElement() const noexcept { return rootElement_.get(); }
  RenderView* renderView() const noexcept;

private:
  void mountRoot(WidgetRef root);

  // Declaration order is teardown order: the element tree unmounts before the
  // pipeline detaches, which is before the build owner drops the render tree --
  // and a region being dropped withdraws from the pointer binding, while a State
  // being disposed releases its ticker, so those two outlive them all.
  PointerBinding pointers_;
  KeyboardBinding keyboard_;
  TickerRegistry tickers_;
  BuildOwner buildOwner_;
  PipelineOwner pipeline_;
  ElementPtr rootElement_;
  RenderView* view_ = nullptr;
  Size surface_;
};

}  // namespace fltr
