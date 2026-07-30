#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "fltr/core/constraints.hpp"
#include "fltr/render/hit_test.hpp"
#include "fltr/render/object.hpp"

namespace fltr {

/// The box protocol: constraints down, size up, one pass.
///
/// A parent hands `layout()` a BoxConstraints and, if it intends to read the
/// result, passes `parentUsesSize = true`. The child sets its own size, which
/// the parent may read *only* if it asked. That is what keeps a relayout
/// boundary sound: a parent that never reads the size cannot observe the child
/// re-laying itself out.
class RenderBox : public RenderObject {
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

  /// Adds this box and every descendant under `position` to `result`, deepest
  /// first, and returns whether anything was hit.
  virtual bool hitTest(HitTestResult& result, Offset position);
  /// Whether this box itself accepts a hit. A plain box is transparent to input.
  virtual bool hitTestSelf(Offset /*position*/) const { return false; }
  virtual bool hitTestChildren(HitTestResult& /*result*/, Offset /*position*/) { return false; }

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
template <class Data>
class RenderBoxContainer : public RenderBox {
public:
  using ChildData = Data;

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

  std::size_t indexOfChild(const RenderBox& child) const {
    for (std::size_t i = 0; i < children_.size(); ++i) {
      if (children_[i].child.get() == &child) return i;
    }
    FLTR_EXPECTS(false, "indexOfChild called for a node that is not a child");
    return children_.size();
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

  /// Permutes slots into `order`. Nothing is adopted or dropped, so a child that
  /// moves keeps its layout state, its paint state, and its per-child data,
  /// which travels in the slot with it.
  void reorderChildren(std::span<RenderBox* const> order) {
    FLTR_EXPECTS(order.size() == children_.size(), "reorderChildren needs the full child list");
    bool moved = false;
    for (std::size_t i = 0; i < order.size() && i < children_.size(); ++i) {
      if (children_[i].child.get() == order[i]) continue;
      std::size_t from = i + 1;
      while (from < children_.size() && children_[from].child.get() != order[i]) ++from;
      if (from == children_.size()) {
        FLTR_EXPECTS(false, "reorderChildren given a node that is not a child");
        break;
      }
      std::swap(children_[i], children_[from]);
      moved = true;
    }
    if (moved) markNeedsLayout();
  }

  void setChildData(std::size_t index, ChildData data) {
    FLTR_EXPECTS(index < children_.size(), "index out of range");
    if (children_[index].data == data) return;
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
