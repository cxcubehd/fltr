#include "fltr/widgets/binding.hpp"

namespace fltr {

WidgetBinding::WidgetBinding(Size surface, TextService& textService)
    : buildOwner_(textService, pointers_), surface_(surface) {}

WidgetBinding::~WidgetBinding() {
  // Unmount before anything is destroyed, so every State sees dispose().
  if (rootElement_) rootElement_->unmount();
  pipeline_.setRootNode(nullptr);
}

RenderView* WidgetBinding::renderView() const noexcept { return view_; }

void WidgetBinding::mountRoot(WidgetRef root) {
  FLTR_EXPECTS(rootElement_ == nullptr, "the binding already has a root");
  FLTR_EXPECTS(root.type() == widgetTypeOf<View>(), "the binding's root must be a View");
  rootElement_ = root->createElement();
  rootElement_->mount(nullptr, buildOwner_);
  view_ = static_cast<RenderView*>(buildOwner_.rootRenderObject());
  FLTR_ENSURES(view_ != nullptr, "mounting the root View produced no render object");
  view_->setSurface(surface_);
  pipeline_.setRootNode(view_);
}

void WidgetBinding::setSurface(Size surface) {
  if (surface == surface_) return;
  surface_ = surface;
  if (RenderView* view = renderView()) view->setSurface(surface);
}

void WidgetBinding::dispatchPointer(const PointerEvent& event) {
  FLTR_EXPECTS(view_ != nullptr, "pointer events need a mounted root");
  pointers_.dispatch(event, *view_);
}

Scene WidgetBinding::drawFrame() {
  // Hover is settled before the build, not after it: the answer is re-resolved
  // against the tree the previous frame left laid out, so whatever an enter or
  // exit callback changes is built in this frame rather than the next.
  if (view_) pointers_.settleHover(*view_);

  buildOwner_.resetBuildCount();
  buildOwner_.flushBuild();
  Scene scene = pipeline_.drawFrame();

  // Layout is what moves boxes. A paint-only change -- the render-attached
  // animation path -- cannot alter what lies under the cursor, which is exactly
  // the invariant that makes markNeedsPaint the cheap path.
  if (pipeline_.stats().layouts > 0) pointers_.mouseTracker().invalidate();
  return scene;
}

}  // namespace fltr
