#include "fltr/render/stack.hpp"

#include <algorithm>

namespace fltr {

void RenderStack::performResize() {
  FLTR_EXPECTS(constraints_.hasBoundedWidth() && constraints_.hasBoundedHeight(),
               "StackFit::Expand requires bounded constraints; there is nothing to expand into");
  setSize(constraints_.biggest());
}

void RenderStack::performLayout() {
  const std::size_t n = children_.size();

  const BoxConstraints nonPositioned = [&] {
    switch (fit_) {
      case StackFit::Loose: return constraints_.loosen();
      case StackFit::Expand: return BoxConstraints::tight(constraints_.biggest());
      case StackFit::Passthrough: return constraints_;
    }
    return constraints_;
  }();

  if (!sizedByParent()) {
    // Size to the largest non-positioned child, then constrain. Positioned
    // children never contribute to the stack's size.
    Size largest = constraints_.smallest();
    bool sawNonPositioned = false;
    for (std::size_t i = 0; i < n; ++i) {
      if (children_[i].data.positioned) continue;
      sawNonPositioned = true;
      const Size s = layoutChildForSize(*children_[i].child, nonPositioned);
      largest = {std::max(largest.width, s.width), std::max(largest.height, s.height)};
    }
    setSize(sawNonPositioned ? constraints_.constrain(largest) : constraints_.smallest());
  } else {
    // performResize already set the size; children just need laying out.
    for (std::size_t i = 0; i < n; ++i) {
      if (children_[i].data.positioned) continue;
      layoutChild(*children_[i].child, nonPositioned);
    }
  }

  for (std::size_t i = 0; i < n; ++i) {
    const StackChildData& d = children_[i].data;
    RenderBox& child = *children_[i].child;

    if (!d.positioned) {
      // A non-positioned child was laid out above; align it within the stack.
      // Its size is readable here only because layoutChildForSize asked for it,
      // or because layoutChild gave it tight constraints under Expand.
      const Size childSize = child.hasSize() ? child.size() : Size::zero();
      setChildOffset(i, alignment_.inscribe(childSize, size_));
      continue;
    }

    // Positioned: derive constraints from whichever edges were given.
    float minW = 0.0f, maxW = kInf, minH = 0.0f, maxH = kInf;
    if (d.left && d.right) {
      minW = maxW = std::max(0.0f, size_.width - *d.left - *d.right);
    } else if (d.width) {
      minW = maxW = std::max(0.0f, *d.width);
    } else {
      maxW = size_.width;
    }
    if (d.top && d.bottom) {
      minH = maxH = std::max(0.0f, size_.height - *d.top - *d.bottom);
    } else if (d.height) {
      minH = maxH = std::max(0.0f, *d.height);
    } else {
      maxH = size_.height;
    }

    const Size childSize = layoutChildForSize(child, BoxConstraints{minW, maxW, minH, maxH});

    float x;
    if (d.left) {
      x = *d.left;
    } else if (d.right) {
      x = size_.width - *d.right - childSize.width;
    } else {
      x = alignment_.inscribe(childSize, size_).dx;
    }
    float y;
    if (d.top) {
      y = *d.top;
    } else if (d.bottom) {
      y = size_.height - *d.bottom - childSize.height;
    } else {
      y = alignment_.inscribe(childSize, size_).dy;
    }
    setChildOffset(i, {x, y});
  }
}

}  // namespace fltr
