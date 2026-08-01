#pragma once

#include "fltr/core/listenable.hpp"
#include "fltr/render/stack.hpp"

namespace fltr {

class RenderAnchor;

// ---------------------------------------------------------------------------
// RenderOverlay
// ---------------------------------------------------------------------------

/// A base subtree with entries drawn above it.
///
/// The first child is the base and alone decides the size: an overlay must never
/// change the layout of what it overlays, however large an entry is. Every other
/// child is an entry, laid out inside that size -- loosely at the origin, or by
/// its edge insets when positioned -- painted after the base and, because
/// hit testing walks children in reverse, tested before it.
class RenderOverlay final : public RenderBoxContainer<StackChildData> {
public:
  const char* typeName() const override { return "Overlay"; }

  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override { defaultPaint(context, offset); }
};

// ---------------------------------------------------------------------------
// Anchoring
// ---------------------------------------------------------------------------

/// The rectangle one widget occupies, named so that something in an overlay can
/// be placed against it.
///
/// Consumer-owned and referred to by pointer, like a scroll controller or a
/// focus node: the anchor marking the target and the entry following it are in
/// different subtrees, and this is the only thing they share.
class AnchorLink final : public Listenable {
public:
  ~AnchorLink() override;

  /// Where the anchor is, in the render tree's root space. Empty while no anchor
  /// names this link and until that anchor has been laid out.
  Rect rect() const;
  bool attached() const noexcept { return anchor_ != nullptr; }

  /// Re-places everything anchored here, at the cost of one repaint. The anchor
  /// calls it whenever it lays out; a consumer whose anchor moved *without*
  /// laying out -- scrolling moves by painting -- calls it too.
  void markMoved() { notifyListeners(); }

private:
  friend class RenderAnchor;

  RenderAnchor* anchor_ = nullptr;
};

/// Publishes through a link the rectangle its child occupies. Layout and
/// painting are its child's, untouched.
class RenderAnchor final : public RenderProxyBox {
public:
  explicit RenderAnchor(AnchorLink* link) { setLink(link); }
  ~RenderAnchor() override { setLink(nullptr); }

  const char* typeName() const override { return "Anchor"; }

  void setLink(AnchorLink* link);
  const AnchorLink* link() const noexcept { return link_; }

  void performLayout() override {
    RenderProxyBox::performLayout();
    if (link_) link_->markMoved();
  }

private:
  friend class AnchorLink;

  AnchorLink* link_ = nullptr;
};

/// Places its child against the rectangle a link names rather than within its
/// own parent.
///
/// The child's *size* is resolved during layout and its *position* during paint,
/// which is the one phase in which every ancestor offset between here and the
/// root is final. That is also what lets an entry follow an anchor that moved
/// without either of them laying out, for the price of a notification.
class RenderAnchored final : public RenderShiftedBox {
public:
  /// Which point on the anchor the child hangs from, which point on the child
  /// lands there, and how far off. A dropdown is bottomLeft to topLeft.
  struct Placement {
    Alignment anchorSide = Alignment::bottomLeft();
    Alignment childSide = Alignment::topLeft();
    Offset offset;
    /// Keeps the child inside the overlay, which is what stops a menu opened
    /// near an edge from hanging off it.
    bool keepOnScreen = true;

    friend bool operator==(const Placement&, const Placement&) noexcept = default;
  };

  RenderAnchored(AnchorLink* link, Placement placement) : placement_(placement) { setLink(link); }

  const char* typeName() const override { return "Anchored"; }
  /// It fills the overlay and places its child inside itself, so its own size
  /// depends on nothing below it.
  bool sizedByParent() const override { return true; }

  void setLink(AnchorLink* link);
  void setPlacement(Placement placement);

  void performResize() override;
  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override;

private:
  /// A link that has been destroyed detached every subscription it handed out,
  /// which is how this knows its pointer is still good.
  const AnchorLink* liveLink() const noexcept { return moved_.attached() ? link_ : nullptr; }

  Offset resolveChildOffset() const;

  AnchorLink* link_ = nullptr;
  Subscription moved_;
  Placement placement_;
};

}  // namespace fltr
