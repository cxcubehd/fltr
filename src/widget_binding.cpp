#include "fltr/widgets/binding.hpp"

namespace fltr {

WidgetBinding::WidgetBinding(Size surface, TextService& textService)
    : buildOwner_(textService), surface_(surface) {}

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

Scene WidgetBinding::drawFrame() {
  buildOwner_.resetBuildCount();
  buildOwner_.flushBuild();
  return pipeline_.drawFrame();
}

}  // namespace fltr
