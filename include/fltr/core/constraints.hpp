#pragma once

#include <algorithm>
#include <optional>
#include <type_traits>

#include "fltr/core/geometry.hpp"

namespace fltr {

/// The "constraints down" half of the layout protocol.
///
/// A parent hands these to a child; the child must return a Size that satisfies
/// them. Because the child cannot see anything else about its parent, layout is
/// a single top-down pass with a single bottom-up result -- linear, not
/// iterative.
struct BoxConstraints {
  float minWidth = 0.0f;
  float maxWidth = kInf;
  float minHeight = 0.0f;
  float maxHeight = kInf;

  static constexpr BoxConstraints tight(Size s) noexcept {
    return {s.width, s.width, s.height, s.height};
  }
  static constexpr BoxConstraints tightFor(float w, float h) noexcept { return {w, w, h, h}; }
  static constexpr BoxConstraints loose(Size s) noexcept { return {0, s.width, 0, s.height}; }
  static constexpr BoxConstraints expand(float w = kInf, float h = kInf) noexcept {
    return {w, w, h, h};
  }
  static constexpr BoxConstraints unbounded() noexcept { return {}; }
  static constexpr BoxConstraints tightWidth(float w) noexcept { return {w, w, 0, kInf}; }
  static constexpr BoxConstraints tightHeight(float h) noexcept { return {0, kInf, h, h}; }

  constexpr bool hasTightWidth() const noexcept { return minWidth >= maxWidth; }
  constexpr bool hasTightHeight() const noexcept { return minHeight >= maxHeight; }
  constexpr bool isTight() const noexcept { return hasTightWidth() && hasTightHeight(); }
  constexpr bool hasBoundedWidth() const noexcept { return maxWidth < kInf; }
  constexpr bool hasBoundedHeight() const noexcept { return maxHeight < kInf; }

  constexpr float constrainWidth(float w = kInf) const noexcept {
    return std::clamp(w, minWidth, maxWidth);
  }
  constexpr float constrainHeight(float h = kInf) const noexcept {
    return std::clamp(h, minHeight, maxHeight);
  }
  constexpr Size constrain(Size s) const noexcept {
    return {constrainWidth(s.width), constrainHeight(s.height)};
  }
  /// The smallest size satisfying these constraints.
  constexpr Size smallest() const noexcept { return {minWidth, minHeight}; }
  /// The largest size satisfying these constraints. Only meaningful when bounded.
  constexpr Size biggest() const noexcept { return {maxWidth, maxHeight}; }

  constexpr BoxConstraints loosen() const noexcept { return {0, maxWidth, 0, maxHeight}; }
  constexpr BoxConstraints tighten(float w, float h) const noexcept {
    BoxConstraints c = *this;
    if (w >= 0) { c.minWidth = c.maxWidth = constrainWidth(w); }
    if (h >= 0) { c.minHeight = c.maxHeight = constrainHeight(h); }
    return c;
  }
  constexpr BoxConstraints widthConstraints() const noexcept {
    return {minWidth, maxWidth, 0, kInf};
  }
  constexpr BoxConstraints heightConstraints() const noexcept {
    return {0, kInf, minHeight, maxHeight};
  }
  /// Shrink by `insets` on each axis, clamping at zero.
  constexpr BoxConstraints deflate(EdgeInsets insets) const noexcept {
    const float h = insets.horizontal();
    const float v = insets.vertical();
    const float dw = std::max(0.0f, maxWidth - h);
    const float dh = std::max(0.0f, maxHeight - v);
    return {std::clamp(minWidth - h, 0.0f, dw), dw, std::clamp(minHeight - v, 0.0f, dh), dh};
  }
  /// Clamp these constraints so they also satisfy `outer`.
  constexpr BoxConstraints enforce(BoxConstraints outer) const noexcept {
    return {std::clamp(minWidth, outer.minWidth, outer.maxWidth),
            std::clamp(maxWidth, outer.minWidth, outer.maxWidth),
            std::clamp(minHeight, outer.minHeight, outer.maxHeight),
            std::clamp(maxHeight, outer.minHeight, outer.maxHeight)};
  }
  /// Replaces only the bounds that are given, keeping the rest.
  constexpr BoxConstraints copyWith(std::optional<float> minW = {}, std::optional<float> maxW = {},
                                    std::optional<float> minH = {},
                                    std::optional<float> maxH = {}) const noexcept {
    return {minW.value_or(minWidth), maxW.value_or(maxWidth), minH.value_or(minHeight),
            maxH.value_or(maxHeight)};
  }

  constexpr bool isNormalized() const noexcept {
    return minWidth >= 0 && minWidth <= maxWidth && minHeight >= 0 && minHeight <= maxHeight;
  }

  friend constexpr bool operator==(BoxConstraints, BoxConstraints) noexcept = default;
};

static_assert(std::is_trivially_destructible_v<BoxConstraints>);

}  // namespace fltr
