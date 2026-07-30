#include "fltr/render/flex.hpp"

#include <algorithm>

namespace fltr {

BoxConstraints RenderFlex::childConstraints(float minMain, float maxMain) const noexcept {
  const bool horizontal = direction_ == Axis::Horizontal;
  const float maxCross = horizontal ? constraints_.maxHeight : constraints_.maxWidth;
  // Stretch only makes sense when the cross axis is bounded; otherwise there is
  // nothing to stretch to and we fall back to loose.
  const bool stretch = crossAlign_ == CrossAxisAlignment::Stretch && maxCross < kInf;
  const float minCross = stretch ? maxCross : 0.0f;
  return horizontal ? BoxConstraints{minMain, maxMain, minCross, maxCross}
                    : BoxConstraints{minCross, maxCross, minMain, maxMain};
}

void RenderFlex::performLayout() {
  const std::size_t n = children_.size();
  const float maxMain =
      direction_ == Axis::Horizontal ? constraints_.maxWidth : constraints_.maxHeight;
  const bool canFlex = maxMain < kInf;
  const float totalSpacing = n > 1 ? spacing_ * static_cast<float>(n - 1) : 0.0f;

  float allocated = 0.0f;
  float crossSize = 0.0f;
  int totalFlex = 0;

  // Pass 1: inflexible children size themselves, which makes the free space
  // knowable without iterating.
  for (std::size_t i = 0; i < n; ++i) {
    const FlexChildData& d = children_[i].data;
    if (d.flex > 0) {
      totalFlex += d.flex;
      continue;
    }
    const Size s = layoutChildForSize(*children_[i].child, childConstraints(0.0f, kInf));
    allocated += mainOf(s);
    crossSize = std::max(crossSize, crossOf(s));
  }

  // Pass 2: flexible children divide what is left.
  if (totalFlex > 0) {
    const float freeSpace = std::max(0.0f, (canFlex ? maxMain : 0.0f) - allocated - totalSpacing);
    const float spacePerFlex = canFlex ? freeSpace / static_cast<float>(totalFlex) : 0.0f;
    int remainingFlex = totalFlex;
    float remainingSpace = freeSpace;

    for (std::size_t i = 0; i < n; ++i) {
      const FlexChildData& d = children_[i].data;
      if (d.flex <= 0) continue;

      float maxChildExtent;
      if (!canFlex) {
        // Unbounded main axis: there is no share to divide, so a flexible child
        // behaves as though it were inflexible.
        maxChildExtent = kInf;
      } else if (remainingFlex == d.flex) {
        // Last flexible child absorbs the rounding remainder, so the children
        // exactly fill the free space rather than leaving a sub-pixel gap.
        maxChildExtent = std::max(0.0f, remainingSpace);
      } else {
        maxChildExtent = spacePerFlex * static_cast<float>(d.flex);
      }
      const float minChildExtent = (d.fit == FlexFit::Tight && canFlex) ? maxChildExtent : 0.0f;

      const Size s =
          layoutChildForSize(*children_[i].child, childConstraints(minChildExtent, maxChildExtent));
      allocated += mainOf(s);
      remainingSpace -= mainOf(s);
      remainingFlex -= d.flex;
      crossSize = std::max(crossSize, crossOf(s));
    }
  }

  const float idealMain =
      (mainSize_ == MainAxisSize::Max && canFlex) ? maxMain : allocated + totalSpacing;
  setSize(constraints_.constrain(direction_ == Axis::Horizontal ? Size{idealMain, crossSize}
                                                                : Size{crossSize, idealMain}));

  const float actualMain = mainOf(size_);
  const float actualCross = crossOf(size_);
  const float remaining = std::max(0.0f, actualMain - allocated - totalSpacing);

  float leading = 0.0f;
  float between = spacing_;
  const float fn = static_cast<float>(n);
  switch (mainAlign_) {
    case MainAxisAlignment::Start: break;
    case MainAxisAlignment::End: leading = remaining; break;
    case MainAxisAlignment::Center: leading = remaining * 0.5f; break;
    case MainAxisAlignment::SpaceBetween:
      if (n > 1) between += remaining / (fn - 1.0f);
      break;
    case MainAxisAlignment::SpaceAround:
      if (n > 0) {
        const float gap = remaining / fn;
        leading = gap * 0.5f;
        between += gap;
      }
      break;
    case MainAxisAlignment::SpaceEvenly:
      if (n > 0) {
        const float gap = remaining / (fn + 1.0f);
        leading = gap;
        between += gap;
      }
      break;
  }

  float position = leading;
  for (std::size_t i = 0; i < n; ++i) {
    const Size childSize = children_[i].child->size();
    float crossOffset = 0.0f;
    switch (crossAlign_) {
      case CrossAxisAlignment::Start:
      case CrossAxisAlignment::Stretch: crossOffset = 0.0f; break;
      case CrossAxisAlignment::End: crossOffset = actualCross - crossOf(childSize); break;
      case CrossAxisAlignment::Center:
        crossOffset = (actualCross - crossOf(childSize)) * 0.5f;
        break;
    }
    setChildOffset(i, makeOffset(position, crossOffset));
    position += mainOf(childSize) + between;
  }
}

}  // namespace fltr
