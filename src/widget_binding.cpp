#include "fltr/widgets/binding.hpp"

namespace fltr {

WidgetBinding::WidgetBinding(Size surface, TextService& textService)
    : buildOwner_(textService), surface_(surface) {}

WidgetBinding::~WidgetBinding() {
  // Unmount before anything is destroyed, so every State sees dispose().
  if (rootElement_) rootElement_->unmount();
  pipeline_.setRootNode(nullptr);
}

RenderView* WidgetBinding::renderView() const noexcept {
  return static_cast<RenderView*>(buildOwner_.rootRenderObject());
}

void WidgetBinding::mountRoot(WidgetRef root) {
  FLTR_EXPECTS(rootElement_ == nullptr, "the binding already has a root");
  rootElement_ = root->createElement();
  rootElement_->mount(nullptr, buildOwner_);
  RenderView* view = renderView();
  FLTR_ENSURES(view != nullptr, "the root widget must produce a RenderView");
  view->setSurface(surface_);
  pipeline_.setRootNode(view);
}

void WidgetBinding::setSurface(Size surface) {
  if (surface == surface_) return;
  surface_ = surface;
  if (RenderView* view = renderView()) view->setSurface(surface);
}

Scene WidgetBinding::drawFrame() {
  buildOwner_.flushBuild();
  return pipeline_.drawFrame();
}

}  // namespace fltr
