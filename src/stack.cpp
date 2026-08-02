#include "fltr/render/stack.hpp"

#include <algorithm>

namespace fltr {

Offset layoutPositionedChild(RenderBox& child, const StackChildData& data, Size size,
                             Alignment alignment) {
  // Derive constraints from whichever edges were given.
  float minW = 0.0f, maxW = kInf, minH = 0.0f, maxH = kInf;
  if (data.left && data.right) {
    minW = maxW = std::max(0.0f, size.width - *data.left - *data.right);
  } else if (data.width) {
    minW = maxW = std::max(0.0f, *data.width);
  } else {
    maxW = size.width;
  }
  if (data.top && data.bottom) {
    minH = maxH = std::max(0.0f, size.height - *data.top - *data.bottom);
  } else if (data.height) {
    minH = maxH = std::max(0.0f, *data.height);
  } else {
    maxH = size.height;
  }

  child.layout(BoxConstraints{minW, maxW, minH, maxH}, true);
  const Size childSize = child.size();

  float x;
  if (data.left) {
    x = *data.left;
  } else if (data.right) {
    x = size.width - *data.right - childSize.width;
  } else {
    x = alignment.inscribe(childSize, size).dx;
  }
  float y;
  if (data.top) {
    y = *data.top;
  } else if (data.bottom) {
    y = size.height - *data.bottom - childSize.height;
  } else {
    y = alignment.inscribe(childSize, size).dy;
  }
  return {x, y};
}

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
    // performResize already set the size. The offset pass below reads each
    // child's size, so this must say so -- which costs nothing, since Expand
    // hands out tight constraints and that alone makes them boundaries.
    for (std::size_t i = 0; i < n; ++i) {
      if (children_[i].data.positioned) continue;
      layoutChildForSize(*children_[i].child, nonPositioned);
    }
  }

  for (std::size_t i = 0; i < n; ++i) {
    const StackChildData& d = children_[i].data;
    RenderBox& child = *children_[i].child;
    // Both branches above measured every non-positioned child, so its size is
    // readable here.
    setChildOffset(i, d.positioned ? layoutPositionedChild(child, d, size_, alignment_)
                                   : alignment_.inscribe(child.size(), size_));
  }
}

}  // namespace fltr
