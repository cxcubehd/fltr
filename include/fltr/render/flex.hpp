#pragma once

#include "fltr/render/box.hpp"
#include "fltr/debug/format.hpp"

namespace fltr {

enum class Axis : std::uint8_t { Horizontal, Vertical };

enum class MainAxisAlignment : std::uint8_t {
  Start,
  End,
  Center,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
};

enum class CrossAxisAlignment : std::uint8_t { Start, End, Center, Stretch };

/// Whether the flex box takes all the main-axis room it is offered, or only as
/// much as its children need.
enum class MainAxisSize : std::uint8_t { Min, Max };

/// Tight: a flexible child must exactly fill its share. Loose: it may take less.
enum class FlexFit : std::uint8_t { Tight, Loose };

struct FlexChildData {
  /// 0 means inflexible: the child sizes itself and keeps whatever it asks for.
  int flex = 0;
  FlexFit fit = FlexFit::Tight;

  friend constexpr bool operator==(FlexChildData, FlexChildData) noexcept = default;
};

/// Row and column layout with flexible children.
///
/// Two passes over the children, both linear: inflexible children are measured
/// first so the remaining free space is known, then flexible children divide
/// that space in proportion to their flex factors.
class RenderFlex final : public RenderBoxContainer<FlexChildData> {
public:
  explicit RenderFlex(Axis direction = Axis::Horizontal,
                      MainAxisAlignment mainAxisAlignment = MainAxisAlignment::Start,
                      CrossAxisAlignment crossAxisAlignment = CrossAxisAlignment::Center,
                      MainAxisSize mainAxisSize = MainAxisSize::Max, float spacing = 0.0f)
      : direction_(direction),
        mainAlign_(mainAxisAlignment),
        crossAlign_(crossAxisAlignment),
        mainSize_(mainAxisSize),
        spacing_(spacing) {}

  const char* typeName() const override {
    return direction_ == Axis::Horizontal ? "Row" : "Column";
  }
  std::string describe() const override {
    static constexpr const char* kMain[] = {"start", "end",        "center",
                                            "between", "around",   "evenly"};
    static constexpr const char* kCross[] = {"start", "end", "center", "stretch"};
    std::string s = std::string("main=") + kMain[static_cast<int>(mainAlign_)] +
                    " cross=" + kCross[static_cast<int>(crossAlign_)];
    if (spacing_ != 0.0f) s += " spacing=" + dbg::str(spacing_);
    return s;
  }

  Axis direction() const noexcept { return direction_; }
  void setDirection(Axis a) {
    if (a == direction_) return;
    direction_ = a;
    markNeedsLayout();
  }
  void setMainAxisAlignment(MainAxisAlignment v) {
    if (v == mainAlign_) return;
    mainAlign_ = v;
    markNeedsLayout();
  }
  void setCrossAxisAlignment(CrossAxisAlignment v) {
    if (v == crossAlign_) return;
    crossAlign_ = v;
    markNeedsLayout();
  }
  void setMainAxisSize(MainAxisSize v) {
    if (v == mainSize_) return;
    mainSize_ = v;
    markNeedsLayout();
  }
  void setSpacing(float v) {
    if (v == spacing_) return;
    spacing_ = v;
    markNeedsLayout();
  }

  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override { defaultPaint(context, offset); }

private:
  float mainOf(Size s) const noexcept {
    return direction_ == Axis::Horizontal ? s.width : s.height;
  }
  float crossOf(Size s) const noexcept {
    return direction_ == Axis::Horizontal ? s.height : s.width;
  }
  Offset makeOffset(float main, float cross) const noexcept {
    return direction_ == Axis::Horizontal ? Offset{main, cross} : Offset{cross, main};
  }
  BoxConstraints childConstraints(float minMain, float maxMain) const noexcept;

  Axis direction_;
  MainAxisAlignment mainAlign_;
  CrossAxisAlignment crossAlign_;
  MainAxisSize mainSize_;
  float spacing_;
};

}  // namespace fltr
