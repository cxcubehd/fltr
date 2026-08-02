#include "fltr/render/overlay.hpp"

#include <algorithm>

namespace fltr {

// ---------------------------------------------------------------------------
// RenderOverlay
// ---------------------------------------------------------------------------

void RenderOverlay::performLayout() {
  if (children_.empty()) {
    setSize(constraints_.smallest());
    return;
  }

  setSize(layoutChildForSize(childAt(0), constraints_));

  const BoxConstraints entryConstraints = BoxConstraints::loose(size_);
  for (std::size_t i = 1; i < children_.size(); ++i) {
    const StackChildData& data = dataAt(i);
    if (data.positioned) {
      setChildOffset(i, layoutPositionedChild(childAt(i), data, size_, Alignment::topLeft()));
      continue;
    }
    // Not measured: an entry that resizes never reaches the overlay, and what it
    // does with the space it was offered is its own business.
    layoutChild(childAt(i), entryConstraints);
    setChildOffset(i, Offset::zero());
  }
}

// ---------------------------------------------------------------------------
// Anchoring
// ---------------------------------------------------------------------------

AnchorLink::~AnchorLink() {
  if (anchor_) anchor_->link_ = nullptr;
}

Rect AnchorLink::rect() const {
  // A detached anchor has no path to a root, so there is no space to answer in.
  if (!anchor_ || !anchor_->attached() || !anchor_->hasSize()) return Rect::zero();
  return anchor_->localToGlobalRect(anchor_->paintBounds());
}

void RenderAnchor::setLink(AnchorLink* link) {
  if (link == link_) return;
  if (link_) {
    link_->anchor_ = nullptr;
    link_->markMoved();
  }
  link_ = link;
  if (!link_) return;
  FLTR_EXPECTS(link_->anchor_ == nullptr, "two anchors named the same link");
  link_->anchor_ = this;
  link_->markMoved();
}

void RenderAnchored::setLink(AnchorLink* link) {
  if (link == link_) return;
  link_ = link;
  observeForPaint(moved_, link);
  markNeedsPaint();
}

void RenderAnchored::setPlacement(Placement placement) {
  if (placement == placement_) return;
  placement_ = placement;
  markNeedsPaint();
}

void RenderAnchored::performResize() {
  FLTR_EXPECTS(constraints_.hasBoundedWidth() && constraints_.hasBoundedHeight(),
               "an anchored entry fills the overlay it is in, so it needs bounded constraints");
  setSize(constraints_.biggest());
}

void RenderAnchored::performLayout() {
  // The size is the child's own, so this reads it; where it goes is decided at
  // paint, when the offsets between here and the root are final.
  if (child_) layoutChildForSize(*child_, constraints_.loosen());
}

void RenderAnchored::paint(PaintingContext& context, Offset offset) {
  if (!child_) return;
  childOffset_ = resolveChildOffset();
  RenderShiftedBox::paint(context, offset);
}

Offset RenderAnchored::resolveChildOffset() const {
  const AnchorLink* link = liveLink();
  const Rect anchor = link ? link->rect() : Rect::zero();
  const Size childSize = child_->size();

  const Offset target =
      globalToLocal(anchor.topLeft() + anchor.size().alongSize(placement_.anchorSide));
  const Offset origin = target - childSize.alongSize(placement_.childSide) + placement_.offset;
  if (!placement_.keepOnScreen) return origin;

  return {std::clamp(origin.dx, 0.0f, std::max(0.0f, size_.width - childSize.width)),
          std::clamp(origin.dy, 0.0f, std::max(0.0f, size_.height - childSize.height))};
}

}  // namespace fltr
