#include "ui/widgets/panel.hh"

#include "fltr/widgets/basic.hpp"

namespace demo {

using fltr::Align;
using fltr::Alignment;
using fltr::BoxConstraints;
using fltr::ConstrainedBox;
using fltr::DecoratedBox;
using fltr::EdgeInsets;
using fltr::Flexible;
using fltr::Padding;
using fltr::SizedBox;
using fltr::Text;
using fltr::WidgetRef;

WidgetRef panel(const Theme& theme, EdgeInsets padding, WidgetRef child) {
  return DecoratedBox::make({
      .decoration = panelDecoration(theme),
      .child = Padding::make({.padding = padding, .child = child}),
  });
}

WidgetRef bare(EdgeInsets padding, WidgetRef child) {
  return Padding::make({.padding = padding, .child = child});
}

WidgetRef gap(float size) { return SizedBox::make({.size = fltr::Size::square(size)}); }

WidgetRef spacer(int flex) {
  return Flexible::make({.flex = flex, .child = SizedBox::make({.size = fltr::Size::zero()})});
}

WidgetRef divider(const Theme& theme) {
  return DecoratedBox::make({
      .decoration = {.color = theme.border},
      .child = SizedBox::make({.size = {fltr::kInf, theme.hairline}}),
  });
}

WidgetRef title(const Theme& theme, std::string_view text) {
  return Text::make({.text = text, .style = titleStyle(theme)});
}

WidgetRef display(const Theme& theme, std::string_view text) {
  return Text::make({.text = text, .style = displayStyle(theme)});
}

WidgetRef body(const Theme& theme, std::string_view text) {
  return Text::make({.text = text, .style = bodyStyle(theme)});
}

WidgetRef label(const Theme& theme, std::string_view text) {
  return Text::make({.text = text, .style = labelStyle(theme)});
}

WidgetRef sheet(fltr::Key key, const Theme& theme, float maxWidth, WidgetRef child) {
  // `Align` with its default alignment is the `Center` that fltr does not ship
  // under that name -- the capability is there, only the shorthand is missing.
  return Align::make({
      .key = key,
      .alignment = Alignment::center(),
      .child = ConstrainedBox::make({
          .constraints = BoxConstraints{.maxWidth = maxWidth},
          .child = Padding::make({.padding = EdgeInsets::all(theme.unit * 3.0f), .child = child}),
      }),
  });
}

}  // namespace demo
