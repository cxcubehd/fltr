#pragma once

#include "fltr/widgets/basic.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

/// The three shorthands the pages actually wanted, and deliberately no more:
/// this is not a layout DSL, and anything longer than a line belongs in a
/// component with a name.

/// Empty space on both axes. In a Row or a Column only the axis in use matters.
inline fltr::WidgetRef gap(float size) { return fltr::SizedBox::make({.size = {size, size}}); }

/// A one-pixel rule, at whatever one pixel means at this scale.
inline fltr::WidgetRef divider(const Theme& theme) {
  return fltr::SizedBox::make({
      .size = {fltr::kInf, theme.hairline()},
      .child = fltr::DecoratedBox::make({.decoration = {.color = theme.border}}),
  });
}

/// Text that shrink-wraps, which is what a label wants inside a Row.
inline fltr::WidgetRef text(std::string_view content, fltr::TextStyle style,
                            int maxLines = 1) {
  return fltr::Text::make({
      .text = content,
      .style = style,
      .maxLines = maxLines,
      .overflow = fltr::TextOverflow::Ellipsis,
  });
}

}  // namespace fltrdemo
