#pragma once

#include <string>
#include <string_view>

#include "fltr/paint/text.hpp"
#include "fltr/render/box.hpp"
#include "fltr/debug/format.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// Decoration
// ---------------------------------------------------------------------------

struct BoxDecoration {
  Color color = Color::transparent();
  BorderRadius radius = BorderRadius::zero();
  Color borderColor = Color::transparent();
  float borderWidth = 0.0f;

  constexpr bool isVisible() const noexcept {
    return color.a != 0 || (borderWidth > 0.0f && borderColor.a != 0);
  }
  friend constexpr bool operator==(const BoxDecoration&, const BoxDecoration&) noexcept = default;
};

inline BoxDecoration lerp(const BoxDecoration& a, const BoxDecoration& b, float t) noexcept {
  return {lerp(a.color, b.color, t), lerp(a.radius, b.radius, t),
          lerp(a.borderColor, b.borderColor, t), lerpF(a.borderWidth, b.borderWidth, t)};
}

// ---------------------------------------------------------------------------
// RenderView -- the root
// ---------------------------------------------------------------------------

/// The pipeline root. Always a repaint boundary, so it owns the display list
/// the consumer submits.
class RenderView final : public RenderProxyBox {
public:
  explicit RenderView(Size surface) : surface_(surface) {}

  void setSurface(Size s) {
    if (s == surface_) return;
    surface_ = s;
    markNeedsLayout();
  }
  Size surface() const noexcept { return surface_; }

  bool isRepaintBoundary() const override { return true; }
  const char* typeName() const override { return "View"; }

  void performLayout() override {
    setSize(surface_);
    // The child gets tight constraints, so it is a relayout boundary and its
    // invalidations never reach the view.
    if (child_) layoutChild(*child_, BoxConstraints::tight(surface_));
  }

private:
  Size surface_;
};

// ---------------------------------------------------------------------------
// RenderPadding
// ---------------------------------------------------------------------------

class RenderPadding final : public RenderShiftedBox {
public:
  explicit RenderPadding(EdgeInsets padding = {}) : padding_(padding) {}

  const char* typeName() const override { return "Padding"; }
  std::string describe() const override { return "pad=" + dbg::str(padding_); }
  EdgeInsets padding() const noexcept { return padding_; }
  void setPadding(EdgeInsets p) {
    if (p == padding_) return;
    padding_ = p;
    markNeedsLayout();
  }

  void performLayout() override {
    if (!child_) {
      setSize(constraints_.constrain({padding_.horizontal(), padding_.vertical()}));
      return;
    }
    const Size childSize = layoutChildForSize(*child_, constraints_.deflate(padding_));
    childOffset_ = padding_.topLeft();
    setSize(constraints_.constrain(padding_.inflateSize(childSize)));
  }

private:
  EdgeInsets padding_;
};

// ---------------------------------------------------------------------------
// RenderPositionedBox -- alignment
// ---------------------------------------------------------------------------

/// Aligns a single child within itself. With no size factor it expands to fill
/// bounded constraints; with a factor it sizes to a multiple of its child.
class RenderPositionedBox final : public RenderShiftedBox {
public:
  explicit RenderPositionedBox(Alignment alignment = Alignment::center(), float widthFactor = -1.0f,
                               float heightFactor = -1.0f)
      : alignment_(alignment), widthFactor_(widthFactor), heightFactor_(heightFactor) {}

  const char* typeName() const override { return "Align"; }
  std::string describe() const override { return "align=" + dbg::str(alignment_); }
  Alignment alignment() const noexcept { return alignment_; }
  void setAlignment(Alignment a) {
    if (a == alignment_) return;
    alignment_ = a;
    // Alignment changes where the child sits, not how big anything is -- but
    // the offset is computed during layout, so this is a layout invalidation.
    // The render-attached animated variant overrides this; see RenderAlignAnimated.
    markNeedsLayout();
  }
  void setSizeFactors(float w, float h) {
    if (w == widthFactor_ && h == heightFactor_) return;
    widthFactor_ = w;
    heightFactor_ = h;
    markNeedsLayout();
  }

  void performLayout() override {
    const bool shrinkWidth = widthFactor_ >= 0.0f || !constraints_.hasBoundedWidth();
    const bool shrinkHeight = heightFactor_ >= 0.0f || !constraints_.hasBoundedHeight();
    if (!child_) {
      setSize(constraints_.constrain({shrinkWidth ? 0.0f : kInf, shrinkHeight ? 0.0f : kInf}));
      return;
    }
    const Size childSize = layoutChildForSize(*child_, constraints_.loosen());
    setSize(constraints_.constrain(
        {shrinkWidth ? childSize.width * (widthFactor_ >= 0.0f ? widthFactor_ : 1.0f) : kInf,
         shrinkHeight ? childSize.height * (heightFactor_ >= 0.0f ? heightFactor_ : 1.0f) : kInf}));
    childOffset_ = alignment_.inscribe(childSize, size_);
  }

protected:
  Alignment alignment_;
  float widthFactor_;
  float heightFactor_;
};

// ---------------------------------------------------------------------------
// RenderConstrainedBox -- fixed and constrained sizing
// ---------------------------------------------------------------------------

class RenderConstrainedBox final : public RenderProxyBox {
public:
  explicit RenderConstrainedBox(BoxConstraints additional = {}) : additional_(additional) {}

  const char* typeName() const override { return "ConstrainedBox"; }
  std::string describe() const override { return "extra=" + dbg::str(additional_); }
  BoxConstraints additionalConstraints() const noexcept { return additional_; }
  void setAdditionalConstraints(BoxConstraints c) {
    if (c == additional_) return;
    FLTR_EXPECTS(c.isNormalized(), "additional constraints must be normalized");
    additional_ = c;
    markNeedsLayout();
  }

  void performLayout() override {
    const BoxConstraints inner = additional_.enforce(constraints_);
    if (child_) {
      setSize(layoutChildForSize(*child_, inner));
    } else {
      setSize(inner.constrain(Size::zero()));
    }
  }

private:
  BoxConstraints additional_;
};

// ---------------------------------------------------------------------------
// RenderDecoratedBox
// ---------------------------------------------------------------------------

class RenderDecoratedBox : public RenderProxyBox {
public:
  explicit RenderDecoratedBox(BoxDecoration decoration = {}) : decoration_(decoration) {}

  const char* typeName() const override { return "DecoratedBox"; }
  std::string describe() const override { return "fill=" + dbg::str(decoration_.color); }
  const BoxDecoration& decoration() const noexcept { return decoration_; }
  void setDecoration(const BoxDecoration& d) {
    if (d == decoration_) return;
    decoration_ = d;
    // Decoration never affects layout, so this is the cheap path.
    markNeedsPaint();
  }

  void paint(PaintingContext& context, Offset offset) override {
    if (decoration_.isVisible()) {
      context.list().drawRRect(Rect::fromOriginSize(offset, size_), decoration_.radius,
                               decoration_.color, decoration_.borderColor, decoration_.borderWidth);
    }
    RenderProxyBox::paint(context, offset);
  }

protected:
  BoxDecoration decoration_;
};

// ---------------------------------------------------------------------------
// RenderOpacity
// ---------------------------------------------------------------------------

class RenderOpacity : public RenderProxyBox {
public:
  explicit RenderOpacity(float opacity = 1.0f) : opacity_(opacity) {}

  const char* typeName() const override { return "Opacity"; }
  std::string describe() const override { return "opacity=" + dbg::str(opacity_); }
  float opacity() const noexcept { return opacity_; }
  void setOpacity(float v) {
    if (v == opacity_) return;
    opacity_ = v;
    markNeedsPaint();  // never layout
  }

  void paint(PaintingContext& context, Offset offset) override {
    context.pushOpacity(opacity_, [this, offset](PaintingContext& c) {
      RenderProxyBox::paint(c, offset);
    });
  }

protected:
  float opacity_;
};

// ---------------------------------------------------------------------------
// RenderTransform
// ---------------------------------------------------------------------------

class RenderTransform : public RenderProxyBox {
public:
  explicit RenderTransform(Transform2D transform = Transform2D::identity(),
                           Alignment origin = Alignment::center())
      : transform_(transform), origin_(origin) {}

  const char* typeName() const override { return "Transform"; }
  std::string describe() const override { return "xf=" + dbg::str(transform_); }
  const Transform2D& transform() const noexcept { return transform_; }
  void setTransform(const Transform2D& t) {
    if (t == transform_) return;
    transform_ = t;
    markNeedsPaint();  // never layout: a transform does not change the box
  }
  void setOrigin(Alignment a) {
    if (a == origin_) return;
    origin_ = a;
    markNeedsPaint();
  }

  void paint(PaintingContext& context, Offset offset) override {
    const Offset pivot = offset + origin_.inscribe(Size::zero(), size_);
    context.pushTransform(transform_, pivot,
                          [this, offset](PaintingContext& c) { RenderProxyBox::paint(c, offset); });
  }

  bool hitTestChildren(HitTestResult& result, Offset position) override {
    if (!child_) return false;
    const Offset pivot = origin_.inscribe(Size::zero(), size_);
    const Transform2D m = Transform2D::aroundOrigin(transform_, pivot);
    return result.addWithPaintTransform(
        m, position, [this](HitTestResult& r, Offset p) { return child_->hitTest(r, p); });
  }

protected:
  Transform2D transform_;
  Alignment origin_;
};

// ---------------------------------------------------------------------------
// RenderRepaintBoundary
// ---------------------------------------------------------------------------

/// Cuts the repaint walk. Anything below repaints into its own display list, so
/// invalidating it never re-records an ancestor.
class RenderRepaintBoundary final : public RenderProxyBox {
public:
  bool isRepaintBoundary() const override { return true; }
  const char* typeName() const override { return "RepaintBoundary"; }
};

// ---------------------------------------------------------------------------
// RenderClipRect
// ---------------------------------------------------------------------------

class RenderClipRect final : public RenderProxyBox {
public:
  explicit RenderClipRect(BorderRadius radius = BorderRadius::zero()) : radius_(radius) {}
  const char* typeName() const override { return "ClipRect"; }
  void setRadius(BorderRadius r) {
    if (r == radius_) return;
    radius_ = r;
    markNeedsPaint();
  }

  void paint(PaintingContext& context, Offset offset) override {
    if (!child_) return;
    context.pushClipRect(Rect::fromOriginSize(offset, size_), radius_,
                         [this, offset](PaintingContext& c) { c.paintChild(*child_, offset); });
  }

private:
  BorderRadius radius_;
};

// ---------------------------------------------------------------------------
// RenderParagraph -- text as an ordinary child of the layout protocol
// ---------------------------------------------------------------------------

/// Resolves a size from constraints exactly like any other box, by asking the
/// TextService to measure into the available width. A real text implementation
/// drops in behind TextService without touching this class or layout.
///
/// The render object owns its text as a std::string. Render objects are
/// long-lived and may own heap memory; widget configs, which are arena-scratch,
/// carry only a std::string_view. The copy happens only when the text actually
/// changes.
class RenderParagraph final : public RenderBox {
public:
  RenderParagraph(TextService* service, std::string_view text, TextStyle style,
                  TextAlign align = TextAlign::Left, int maxLines = 0)
      : service_(service), text_(text), style_(style), align_(align), maxLines_(maxLines) {
    FLTR_EXPECTS(service_ != nullptr, "RenderParagraph requires a TextService");
  }
  ~RenderParagraph() override { releaseHandle(); }

  const char* typeName() const override { return "Paragraph"; }
  std::string describe() const override { return "\"" + text_ + "\""; }
  std::string_view text() const noexcept { return text_; }
  const TextStyle& style() const noexcept { return style_; }
  ParagraphHandle paragraph() const noexcept { return handle_; }

  void setText(std::string_view t) {
    if (t == text_) return;
    text_.assign(t);
    markNeedsLayout();
  }
  void setStyle(const TextStyle& s) {
    if (s == style_) return;
    // Colour alone does not change metrics, but everything else does; keeping
    // this one branch honest is cheaper than getting it subtly wrong.
    const bool metricsChanged = s.font != style_.font || s.size != style_.size ||
                                s.letterSpacing != style_.letterSpacing ||
                                s.lineHeight != style_.lineHeight;
    style_ = s;
    if (metricsChanged) {
      markNeedsLayout();
    } else {
      markNeedsPaint();
    }
  }
  void setAlign(TextAlign a) {
    if (a == align_) return;
    align_ = a;
    markNeedsLayout();
  }
  void setMaxLines(int n) {
    if (n == maxLines_) return;
    maxLines_ = n;
    markNeedsLayout();
  }

  void performLayout() override {
    releaseHandle();
    const TextSpan span{text_, style_};
    ParagraphSpec spec;
    spec.spans = {&span, 1};
    spec.align = align_;
    spec.maxLines = maxLines_;
    handle_ = service_->acquire(spec, constraints_.maxWidth);
    setSize(constraints_.constrain(service_->metrics(handle_).size));
  }

  void paint(PaintingContext& context, Offset offset) override {
    if (handle_ != kNullParagraph) context.list().drawParagraph(handle_, offset, style_.color);
  }

  /// Byte offset in the source text at a local position; the seam for future
  /// selection and caret work.
  std::uint32_t byteOffsetAt(Offset local) const {
    return handle_ == kNullParagraph ? 0 : service_->byteOffsetAt(handle_, local);
  }

private:
  void releaseHandle() {
    if (handle_ != kNullParagraph) {
      service_->release(handle_);
      handle_ = kNullParagraph;
    }
  }

  TextService* service_;
  std::string text_;
  TextStyle style_;
  TextAlign align_;
  int maxLines_;
  ParagraphHandle handle_ = kNullParagraph;
};

// ---------------------------------------------------------------------------
// RenderSprite -- an image or icon, referenced by opaque handle
// ---------------------------------------------------------------------------

class RenderSprite final : public RenderBox {
public:
  RenderSprite(ImageHandle image, Size preferredSize, Rect source = Rect::zero(),
               Color tint = Color{255, 255, 255, 255})
      : image_(image), preferred_(preferredSize), source_(source), tint_(tint) {}

  const char* typeName() const override { return "Sprite"; }
  std::string describe() const override { return "image=" + std::to_string(image_); }
  ImageHandle image() const noexcept { return image_; }

  void setImage(ImageHandle h) {
    if (h == image_) return;
    image_ = h;
    markNeedsPaint();
  }
  void setPreferredSize(Size s) {
    if (s == preferred_) return;
    preferred_ = s;
    markNeedsLayout();
  }
  void setSource(Rect r) {
    if (r == source_) return;
    source_ = r;
    markNeedsPaint();
  }
  void setTint(Color c) {
    if (c == tint_) return;
    tint_ = c;
    markNeedsPaint();
  }

  void performLayout() override { setSize(constraints_.constrain(preferred_)); }

  void paint(PaintingContext& context, Offset offset) override {
    context.list().drawImage(Rect::fromOriginSize(offset, size_), source_, image_, tint_);
  }

private:
  ImageHandle image_;
  Size preferred_;
  Rect source_;
  Color tint_;
};

}  // namespace fltr
