#pragma once

#include <memory>
#include <vector>

#include "fltr/core/constraints.hpp"
#include "fltr/gestures/hit_test.hpp"
#include "fltr/render/object.hpp"

namespace fltr {

/// The box protocol: constraints down, size up, one pass.
///
/// A parent hands `layout()` a BoxConstraints and, if it intends to read the
/// result, passes `parentUsesSize = true`. The child returns nothing; it sets
/// its own size, which the parent may then read *only* if it asked. Enforcing
/// that is what keeps the relayout boundary sound: if the parent never reads the
/// size, the child can re-lay-out without involving the parent at all.
class RenderBox : public RenderObject, public HitTestTarget {
public:
  ~RenderBox() override;

  /// The single entry point for laying out a child. Never call performLayout
  /// directly.
  void layout(BoxConstraints constraints, bool parentUsesSize = false);
  void layoutWithoutResize() override;

  Size size() const;
  bool hasSize() const noexcept { return hasSize_; }
  BoxConstraints constraints() const noexcept { return constraints_; }

  Rect paintBounds() const override { return Rect::fromOriginSize(Offset::zero(), size_); }

  // --- hit testing --------------------------------------------------------

  /// Returns true if this box or one of its descendants was hit, adding the hit
  /// entries deepest-first.
  virtual bool hitTest(HitTestResult& result, Offset position);
  /// Does this box itself accept a hit at `position`? Default false: a plain box
  /// is transparent to input.
  virtual bool hitTestSelf(Offset position) const { return (void)position, false; }
  virtual bool hitTestChildren(HitTestResult& result, Offset position) {
    return (void)result, (void)position, false;
  }

  void handleEvent(const PointerEvent&, Offset) override {}
  const char* targetName() const override { return typeName(); }

  void paint(PaintingContext&, Offset) override {}
  void visitChildren(FunctionRef<void(RenderObject&)>) const override {}

protected:
  RenderBox() = default;

  /// Compute and set this box's size from `constraints()` alone. Only called
  /// when `sizedByParent()` is true, and children may not be laid out here.
  virtual void performResize() {}

  /// Lay out children and set this box's size.
  virtual void performLayout() = 0;

  /// May only be called from this object's own performLayout / performResize,
  /// and the result must satisfy the incoming constraints.
  void setSize(Size s) {
    FLTR_EXPECTS(doingThisLayout_ || doingThisResize_,
                 "a render box may only set its size during its own layout");
    FLTR_EXPECTS(nearlyEqual(constraints_.constrainWidth(s.width), s.width) &&
                     nearlyEqual(constraints_.constrainHeight(s.height), s.height),
                 "performLayout produced a size that violates its constraints");
    size_ = s;
    hasSize_ = true;
  }

  /// Lay out a child that this box will *not* measure. The child becomes a
  /// relayout boundary, so its own invalidations never reach this box.
  static void layoutChild(RenderBox& child, BoxConstraints c) { child.layout(c, false); }
  /// Lay out a child whose size this box needs. Invalidations propagate here.
  static Size layoutChildForSize(RenderBox& child, BoxConstraints c) {
    child.layout(c, true);
    return child.size();
  }

  Size size_{};
  BoxConstraints constraints_{};

private:
  friend class RenderObject;
  bool canParentUseSize_ = false;
  bool hasSize_ = false;
  bool doingThisLayout_ = false;
  bool doingThisResize_ = false;
};

// ---------------------------------------------------------------------------
// Single-child bases
// ---------------------------------------------------------------------------

/// A box with exactly one optional child, laid out under this box's own
/// constraints and painted at the same origin.
class RenderProxyBox : public RenderBox {
public:
  ~RenderProxyBox() override;

  RenderBox* child() const noexcept { return child_.get(); }
  void setChild(std::unique_ptr<RenderBox> child);
  std::unique_ptr<RenderBox> takeChild();

  void visitChildren(FunctionRef<void(RenderObject&)> visitor) const override {
    if (child_) visitor(*child_);
  }
  const char* typeName() const override { return "ProxyBox"; }

  void performLayout() override;
  void paint(PaintingContext& context, Offset offset) override;
  bool hitTestChildren(HitTestResult& result, Offset position) override;

protected:
  std::unique_ptr<RenderBox> child_;
};

/// A single-child box that positions its child somewhere other than its own
/// origin. Padding and alignment are both this shape.
class RenderShiftedBox : public RenderProxyBox {
public:
  const char* typeName() const override { return "ShiftedBox"; }
  Offset childOffset() const noexcept { return childOffset_; }

  void paint(PaintingContext& context, Offset offset) override;
  bool hitTestChildren(HitTestResult& result, Offset position) override;
  void visitChildrenWithOffsets(FunctionRef<void(RenderObject&, Offset)> visitor) const override {
    if (child_) visitor(*child_, childOffset_);
  }

protected:
  Offset childOffset_{};
};

// ---------------------------------------------------------------------------
// Multi-child base
// ---------------------------------------------------------------------------

/// A box with an ordered list of children plus per-child layout data.
///
/// DIVERGENCE: Flutter attaches a heap-allocated ParentData object to each
/// child, so a child carries data whose type is decided by its parent. We store
/// that data in the parent's own child list instead. It is statically typed, it
/// costs no allocation, and a child cannot be asked for parent data belonging to
/// a different parent. The cost is that a child cannot read its own parent data
/// without going through the parent, which nothing in this framework needs.
template <class ChildData>
class RenderBoxContainer : public RenderBox {
public:
  struct Slot {
    std::unique_ptr<RenderBox> child;
    ChildData data{};
    Offset offset{};
  };

  ~RenderBoxContainer() override {
    for (auto& s : children_) {
      if (s.child) dropChild(s.child.get());
    }
  }

  std::size_t childCount() const noexcept { return children_.size(); }
  RenderBox& childAt(std::size_t i) const {
    FLTR_EXPECTS(i < children_.size(), "child index out of range");
    return *children_[i].child;
  }
  ChildData& dataAt(std::size_t i) {
    FLTR_EXPECTS(i < children_.size(), "child index out of range");
    return children_[i].data;
  }
  const ChildData& dataAt(std::size_t i) const {
    FLTR_EXPECTS(i < children_.size(), "child index out of range");
    return children_[i].data;
  }
  Offset childOffsetAt(std::size_t i) const {
    FLTR_EXPECTS(i < children_.size(), "child index out of range");
    return children_[i].offset;
  }

  void insertChild(std::size_t index, std::unique_ptr<RenderBox> child, ChildData data = {}) {
    FLTR_EXPECTS(child != nullptr, "cannot insert a null child");
    FLTR_EXPECTS(index <= children_.size(), "insert index out of range");
    RenderBox* raw = child.get();
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index),
                     Slot{std::move(child), std::move(data), Offset{}});
    adoptChild(raw);
  }

  void addChild(std::unique_ptr<RenderBox> child, ChildData data = {}) {
    insertChild(children_.size(), std::move(child), std::move(data));
  }

  std::unique_ptr<RenderBox> removeChildAt(std::size_t index) {
    FLTR_EXPECTS(index < children_.size(), "remove index out of range");
    std::unique_ptr<RenderBox> child = std::move(children_[index].child);
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
    if (child) dropChild(child.get());
    return child;
  }

  /// Reorders without detaching, so a keyed child that moves keeps its layout
  /// and paint state.
  void moveChild(std::size_t from, std::size_t to) {
    FLTR_EXPECTS(from < children_.size() && to < children_.size(), "move index out of range");
    if (from == to) return;
    Slot slot = std::move(children_[from]);
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(from));
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(to), std::move(slot));
    markNeedsLayout();
  }

  void setChildData(std::size_t index, ChildData data) {
    FLTR_EXPECTS(index < children_.size(), "index out of range");
    children_[index].data = std::move(data);
    markNeedsLayout();
  }

  void visitChildren(FunctionRef<void(RenderObject&)> visitor) const override {
    for (const auto& s : children_) {
      if (s.child) visitor(*s.child);
    }
  }

  void visitChildrenWithOffsets(FunctionRef<void(RenderObject&, Offset)> visitor) const override {
    for (const auto& s : children_) {
      if (s.child) visitor(*s.child, s.offset);
    }
  }

  /// Paints children in order, so later children paint on top.
  void defaultPaint(PaintingContext& context, Offset offset) {
    for (const auto& s : children_) {
      if (s.child) context.paintChild(*s.child, offset + s.offset);
    }
  }

  /// Hit tests children in reverse order, so the topmost child wins.
  bool hitTestChildren(HitTestResult& result, Offset position) override {
    for (std::size_t i = children_.size(); i-- > 0;) {
      const Slot& s = children_[i];
      if (!s.child) continue;
      const bool hit = result.addWithPaintOffset(
          s.offset, position,
          [&s](HitTestResult& r, Offset p) { return s.child->hitTest(r, p); });
      if (hit) return true;
    }
    return false;
  }

protected:
  void setChildOffset(std::size_t i, Offset o) {
    FLTR_EXPECTS(i < children_.size(), "index out of range");
    children_[i].offset = o;
  }

  std::vector<Slot> children_;
};

}  // namespace fltr
