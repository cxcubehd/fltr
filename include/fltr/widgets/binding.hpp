#pragma once

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
    mountRoot(View::make({.child = buildRoot()}));
  }

  void setSurface(Size surface);
  Size surface() const noexcept { return surface_; }

  /// One frame. Builds what is dirty, then lays out and paints what that made
  /// dirty. A frame with nothing dirty does no work in any of the three phases
  /// and returns a Scene whose revision is unchanged.
  Scene drawFrame();

  bool needsFrame() const noexcept {
    return buildOwner_.needsBuild() || pipeline_.needsFrame();
  }

  BuildOwner& buildOwner() noexcept { return buildOwner_; }
  PipelineOwner& pipeline() noexcept { return pipeline_; }
  Element* rootElement() const noexcept { return rootElement_.get(); }
  RenderView* renderView() const noexcept;

private:
  void mountRoot(WidgetRef root);

  BuildOwner buildOwner_;
  PipelineOwner pipeline_;
  ElementPtr rootElement_;
  Size surface_;
};

}  // namespace fltr
