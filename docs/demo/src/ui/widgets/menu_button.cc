#include "ui/widgets/menu_button.hh"

#include "fltr/components/button.hpp"
#include "fltr/widgets/basic.hpp"
#include "ui/widgets/panel.hh"
#include "ui/widgets/styled_surface.hh"

namespace demo {

using fltr::Column;
using fltr::CrossAxisAlignment;
using fltr::EdgeInsets;
using fltr::MainAxisSize;
using fltr::Padding;
using fltr::RawButton;
using fltr::Row;
using fltr::Text;
using fltr::TextStyle;
using fltr::WidgetRef;

namespace {

/// Text has no state-driven colour: `Text` takes a fixed `TextStyle` and there
/// is no animated text colour in fltr. Everything the label needs is known at
/// build time anyway -- enabled and selected are configuration, not interaction
/// -- so the label never has to rebuild for hover or press.
TextStyle labelFor(const Theme& theme, const MenuButtonSpec& spec) {
  TextStyle style = bodyStyle(theme);
  if (!spec.enabled) {
    style.color = theme.textFaint;
  } else if (spec.selected) {
    style.color = theme.accent;
  }
  return style;
}

WidgetRef shell(const Theme& theme, const MenuButtonSpec& spec, WidgetRef body) {
  SurfaceStyle style = spec.selected ? listItemStyle(theme) : buttonStyle(theme);
  style.pressScale = 0.985f;

  return RawButton::make({
      .key = spec.key,
      .onPressed = spec.onPressed,
      .enabled = spec.enabled,
      .selected = spec.selected,
      .autofocus = spec.autofocus,
      .child = StyledSurface::make({.style = style, .child = body}),
  });
}

}  // namespace

WidgetRef menuButton(const Theme& theme, const MenuButtonSpec& spec) {
  TextStyle trailing = labelStyle(theme);
  if (!spec.enabled) trailing.color = theme.textFaint;

  return shell(theme, spec,
               Padding::make({
                   .padding = EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
                   .child = Row::make({
                       .children =
                           {
                               Text::make({.text = spec.label, .style = labelFor(theme, spec)}),
                               spacer(),
                               Text::make({.text = spec.trailing, .style = trailing}),
                           },
                   }),
               }));
}

WidgetRef backButton(const Theme& theme, fltr::Callback<void()> onPressed) {
  return fltr::ConstrainedBox::make({
      .constraints = {.maxWidth = 128.0f},
      .child = menuButton(theme,
                          {
                              .key = fltr::Key::of("nav.back"),
                              .label = "Back",
                              .trailing = "ESC",
                              .onPressed = onPressed,
                          }),
  });
}

WidgetRef listCard(const Theme& theme, const MenuButtonSpec& spec, std::string_view blurb) {
  TextStyle trailing = labelStyle(theme);
  if (!spec.enabled) trailing.color = theme.textFaint;

  return shell(
      theme, spec,
      Padding::make({
          .padding = EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.5f),
          .child = Column::make({
              .crossAxisAlignment = CrossAxisAlignment::Stretch,
              .mainAxisSize = MainAxisSize::Min,
              .spacing = theme.unit * 0.5f,
              .children =
                  {
                      Row::make({
                          .children =
                              {
                                  Text::make(
                                      {.text = spec.label, .style = labelFor(theme, spec)}),
                                  spacer(),
                                  Text::make({.text = spec.trailing, .style = trailing}),
                              },
                      }),
                      Text::make({.text = blurb, .style = labelStyle(theme)}),
                  },
          }),
      }));
}

}  // namespace demo
