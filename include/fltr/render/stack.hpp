#pragma once

#include <optional>

#include "fltr/render/box.hpp"
#include "fltr/debug/format.hpp"

namespace fltr {

/// How a Stack sizes its non-positioned children.
enum class StackFit : std::uint8_t {
  /// Children get loosened constraints and the stack sizes to the largest.
  Loose,
  /// Children are forced to the stack's own size. The stack's size then depends
  /// only on its constraints, which makes it sizedByParent.
  Expand,
  /// Children get the stack's constraints unchanged.
  Passthrough,
};

/// Per-child positioning. A child with `positioned == false` is aligned by the
/// stack's alignment; a positioned child is placed by its edge insets.
struct StackChildData {
  bool positioned = false;
  std::optional<float> left, top, right, bottom, width, height;

  friend bool operator==(const StackChildData&, const StackChildData&) noexcept = default;
};

static_assert(std::is_trivially_destructible_v<StackChildData>);

class RenderStack final : public RenderBoxContainer<StackChildData> {
public:
  explicit RenderStack(Alignment alignment = Alignment::topLeft(), StackFit fit = StackFit::Loose)
      : alignment_(alignment), fit_(fit) {}

  const char* typeName() const override { return "Stack"; }
  std::string describe() const override {
    static constexpr const char* kFit[] = {"loose", "expand", "passthrough"};
    return std::string("fit=") + kFit[static_cast<int>(fit_)] + " align=" + dbg::str(alignment_);
  }

  /// Under Expand the size comes from constraints alone, so the stack is always
  /// a relayout boundary and a resizing child never reaches its parent.
  bool sizedByParent() const override { return fit_ == StackFit::Expand; }

  void setAlignment(Alignment a) {
    if (a == alignment_) return;
    alignment_ = a;
    markNeedsLayout();
  }
  void setFit(StackFit f) {
    if (f == fit_) return;
    const bool wasSizedByParent = sizedByParent();
    fit_ = f;
    if (wasSizedByParent != sizedByParent()) {
      markNeedsLayoutForSizedByParentChange();
    } else {
      markNeedsLayout();
    }
  }

  void performResize() override;
  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override { defaultPaint(context, offset); }

private:
  Alignment alignment_;
  StackFit fit_;
};

}  // namespace fltr
