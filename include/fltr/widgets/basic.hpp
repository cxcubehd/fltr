#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "fltr/core/callback.hpp"
#include "fltr/gestures/pointer_region.hpp"
#include "fltr/render/boxes.hpp"
#include "fltr/render/flex.hpp"
#include "fltr/render/stack.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// Root
// ---------------------------------------------------------------------------

/// The root of every widget tree. Its size is the surface the consumer gives the
/// binding, not configuration, so it takes no size argument here.
class View final : public Configure<View, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    WidgetRef child;
  };
  using Render = RenderView;

  explicit View(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "View"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderView> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderView>(Size::zero());
  }
  void updateRenderObject(BuildContext&, RenderView&) const {}

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Single-child layout
// ---------------------------------------------------------------------------

class Padding final : public Configure<Padding, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    EdgeInsets padding;
    WidgetRef child;
  };
  using Render = RenderPadding;

  explicit Padding(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Padding"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderPadding> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderPadding>(args_.padding);
  }
  void updateRenderObject(BuildContext&, RenderPadding& render) const {
    render.setPadding(args_.padding);
  }

private:
  Args args_;
};

class Align final : public Configure<Align, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    Alignment alignment = Alignment::center();
    /// Negative means "fill the available space on this axis".
    float widthFactor = -1.0f;
    float heightFactor = -1.0f;
    WidgetRef child;
  };
  using Render = RenderPositionedBox;

  explicit Align(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Align"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderPositionedBox> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderPositionedBox>(args_.alignment, args_.widthFactor,
                                                 args_.heightFactor);
  }
  void updateRenderObject(BuildContext&, RenderPositionedBox& render) const {
    render.setAlignment(args_.alignment);
    render.setSizeFactors(args_.widthFactor, args_.heightFactor);
  }

private:
  Args args_;
};

class ConstrainedBox final : public Configure<ConstrainedBox, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    BoxConstraints constraints;
    WidgetRef child;
  };
  using Render = RenderConstrainedBox;

  explicit ConstrainedBox(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "ConstrainedBox"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderConstrainedBox> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderConstrainedBox>(args_.constraints);
  }
  void updateRenderObject(BuildContext&, RenderConstrainedBox& render) const {
    render.setAdditionalConstraints(args_.constraints);
  }

private:
  Args args_;
};

class SizedBox final : public Configure<SizedBox, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    Size size;
    WidgetRef child;
  };
  using Render = RenderConstrainedBox;

  explicit SizedBox(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "SizedBox"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderConstrainedBox> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderConstrainedBox>(BoxConstraints::tight(args_.size));
  }
  void updateRenderObject(BuildContext&, RenderConstrainedBox& render) const {
    render.setAdditionalConstraints(BoxConstraints::tight(args_.size));
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

class DecoratedBox final : public Configure<DecoratedBox, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    BoxDecoration decoration;
    WidgetRef child;
  };
  using Render = RenderDecoratedBox;

  explicit DecoratedBox(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "DecoratedBox"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderDecoratedBox> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderDecoratedBox>(args_.decoration);
  }
  void updateRenderObject(BuildContext&, RenderDecoratedBox& render) const {
    render.setDecoration(args_.decoration);
  }

private:
  Args args_;
};

class Opacity final : public Configure<Opacity, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    float opacity = 1.0f;
    WidgetRef child;
  };
  using Render = RenderOpacity;

  explicit Opacity(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Opacity"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderOpacity> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderOpacity>(args_.opacity);
  }
  void updateRenderObject(BuildContext&, RenderOpacity& render) const {
    render.setOpacity(args_.opacity);
  }

private:
  Args args_;
};

class Transform final : public Configure<Transform, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    Transform2D transform = Transform2D::identity();
    Alignment origin = Alignment::center();
    WidgetRef child;
  };
  using Render = RenderTransform;

  explicit Transform(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Transform"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderTransform> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderTransform>(args_.transform, args_.origin);
  }
  void updateRenderObject(BuildContext&, RenderTransform& render) const {
    render.setTransform(args_.transform);
    render.setOrigin(args_.origin);
  }

private:
  Args args_;
};

class ClipRect final : public Configure<ClipRect, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    BorderRadius radius = BorderRadius::zero();
    WidgetRef child;
  };
  using Render = RenderClipRect;

  explicit ClipRect(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "ClipRect"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderClipRect> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderClipRect>(args_.radius);
  }
  void updateRenderObject(BuildContext&, RenderClipRect& render) const {
    render.setRadius(args_.radius);
  }

private:
  Args args_;
};

class RepaintBoundary final : public Configure<RepaintBoundary, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    WidgetRef child;
  };
  using Render = RenderRepaintBoundary;

  explicit RepaintBoundary(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "RepaintBoundary"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderRepaintBoundary> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderRepaintBoundary>();
  }
  void updateRenderObject(BuildContext&, RenderRepaintBoundary&) const {}

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Flex
// ---------------------------------------------------------------------------

template <Axis kDirection>
class FlexWidget : public MultiChildRenderObjectWidget {
public:
  struct Args {
    Key key;
    MainAxisAlignment mainAxisAlignment = MainAxisAlignment::Start;
    CrossAxisAlignment crossAxisAlignment = CrossAxisAlignment::Center;
    MainAxisSize mainAxisSize = MainAxisSize::Max;
    float spacing = 0.0f;
    WidgetList children;
  };
  using Render = RenderFlex;

  explicit FlexWidget(const Args& args) : MultiChildRenderObjectWidget(args.key), args_(args) {}

  WidgetList children() const noexcept { return args_.children; }

  std::unique_ptr<RenderFlex> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderFlex>(kDirection, args_.mainAxisAlignment,
                                        args_.crossAxisAlignment, args_.mainAxisSize,
                                        args_.spacing);
  }
  void updateRenderObject(BuildContext&, RenderFlex& render) const {
    render.setMainAxisAlignment(args_.mainAxisAlignment);
    render.setCrossAxisAlignment(args_.crossAxisAlignment);
    render.setMainAxisSize(args_.mainAxisSize);
    render.setSpacing(args_.spacing);
  }

protected:
  ~FlexWidget() = default;

private:
  Args args_;
};

class Row final : public Configure<Row, FlexWidget<Axis::Horizontal>> {
public:
  using Configure::Configure;
  const char* name() const noexcept override { return "Row"; }
};

class Column final : public Configure<Column, FlexWidget<Axis::Vertical>> {
public:
  using Configure::Configure;
  const char* name() const noexcept override { return "Column"; }
};

/// Gives its child a share of a Row's or Column's free main-axis space. `flex`
/// is a weight, not a size, and `FlexFit::Loose` lets the child take less than
/// its share.
class Flexible final : public Configure<Flexible, ParentDataWidget> {
public:
  struct Args {
    Key key;
    int flex = 1;
    FlexFit fit = FlexFit::Tight;
    WidgetRef child;
  };
  using ChildData = FlexChildData;

  explicit Flexible(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Flexible"; }
  WidgetRef child() const noexcept { return args_.child; }
  FlexChildData childData() const noexcept { return {args_.flex, args_.fit}; }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Stack
// ---------------------------------------------------------------------------

class Stack final : public Configure<Stack, MultiChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    Alignment alignment = Alignment::topLeft();
    StackFit fit = StackFit::Loose;
    WidgetList children;
  };
  using Render = RenderStack;

  explicit Stack(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Stack"; }
  WidgetList children() const noexcept { return args_.children; }

  std::unique_ptr<RenderStack> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderStack>(args_.alignment, args_.fit);
  }
  void updateRenderObject(BuildContext&, RenderStack& render) const {
    render.setAlignment(args_.alignment);
    render.setFit(args_.fit);
  }

private:
  Args args_;
};

/// Places its child by edge insets within a Stack instead of by the stack's
/// alignment. Any subset of the edges may be given.
class Positioned final : public Configure<Positioned, ParentDataWidget> {
public:
  struct Args {
    Key key;
    std::optional<float> left, top, right, bottom, width, height;
    WidgetRef child;
  };
  using ChildData = StackChildData;

  explicit Positioned(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Positioned"; }
  WidgetRef child() const noexcept { return args_.child; }
  StackChildData childData() const noexcept {
    return {true, args_.left, args_.top, args_.right, args_.bottom, args_.width, args_.height};
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

/// Taps and hover over the area its child occupies.
///
/// The two are separate mechanisms sharing one region: a tap is contested in the
/// arena and only the winner is told, while hover is resolved by diffing what
/// lies under the cursor and is never contested at all.
///
/// A callback is copied into the region and is invoked from `dispatchPointer`
/// or, for enter and exit, from the start of a frame. Anything it captures by
/// reference must outlive the widget.
class Pointer final : public Configure<Pointer, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    HitTestBehavior behavior = HitTestBehavior::DeferToChild;
    Callback<void()> onEnter;
    Callback<void()> onExit;
    /// Fires when this region wins the pointer, which is when press feedback is
    /// correct to show -- not necessarily the instant the pointer went down.
    Callback<void()> onTapDown;
    Callback<void()> onTap;
    Callback<void()> onTapCancel;
    WidgetRef child;
  };
  using Render = RenderPointerRegion;

  explicit Pointer(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Pointer"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderPointerRegion> createRenderObject(BuildContext& context) const {
    auto region = std::make_unique<RenderPointerRegion>(context.pointerBinding(), args_.behavior);
    region->setCallbacks(callbacks());
    return region;
  }
  void updateRenderObject(BuildContext&, RenderPointerRegion& render) const {
    render.setBehavior(args_.behavior);
    render.setCallbacks(callbacks());
  }

private:
  PointerCallbacks callbacks() const noexcept {
    return {args_.onEnter, args_.onExit, args_.onTapDown, args_.onTap, args_.onTapCancel};
  }

  Args args_;
};

// ---------------------------------------------------------------------------
// Leaves
// ---------------------------------------------------------------------------

/// `text` is not copied into the arena: it must point at storage that outlives
/// the build, which a literal or a game-owned buffer does.
class Text final : public Configure<Text, LeafRenderObjectWidget> {
public:
  struct Args {
    Key key;
    std::string_view text;
    TextStyle style;
    TextAlign align = TextAlign::Left;
    /// Zero means unlimited.
    int maxLines = 0;
    TextOverflow overflow = TextOverflow::Clip;
  };
  using Render = RenderParagraph;

  explicit Text(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Text"; }

  std::unique_ptr<RenderParagraph> createRenderObject(BuildContext& context) const {
    return std::make_unique<RenderParagraph>(&context.textService(), args_.text, args_.style,
                                             args_.align, args_.maxLines, args_.overflow);
  }
  void updateRenderObject(BuildContext&, RenderParagraph& render) const {
    render.setText(args_.text);
    render.setStyle(args_.style);
    render.setAlign(args_.align);
    render.setMaxLines(args_.maxLines);
    render.setOverflow(args_.overflow);
  }

private:
  Args args_;
};

class Sprite final : public Configure<Sprite, LeafRenderObjectWidget> {
public:
  struct Args {
    Key key;
    ImageHandle image = 0;
    Size size;
    Rect source = Rect::zero();
    Color tint = Color{255, 255, 255, 255};
  };
  using Render = RenderSprite;

  explicit Sprite(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Sprite"; }

  std::unique_ptr<RenderSprite> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderSprite>(args_.image, args_.size, args_.source, args_.tint);
  }
  void updateRenderObject(BuildContext&, RenderSprite& render) const {
    render.setImage(args_.image);
    render.setPreferredSize(args_.size);
    render.setSource(args_.source);
    render.setTint(args_.tint);
  }

private:
  Args args_;
};

}  // namespace fltr
