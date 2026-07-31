#include "app/pages.hpp"

#include "fltr/widgets/basic.hpp"
#include "ui/button.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

namespace {

constexpr std::string_view kVersion = "fltr 0.1.0  /  raylib 6.0  /  demo build 9";

WidgetRef menuButton(const Theme& theme, std::string_view label, Callback<void()> onPressed,
                     bool enabled = true) {
  return Padding::make({
      .padding = EdgeInsets::symmetric(0.0f, theme.unit() * 0.4f),
      .child = Button::make({
          .label = label,
          .onPressed = onPressed,
          .enabled = enabled,
          .kind = ButtonKind::Menu,
          .width = theme.menuButtonWidth(),
      }),
  });
}

}  // namespace

WidgetRef buildTitlePage(BuildContext& context, AppState& app) {
  const Theme& theme = themeOf(context);
  AppState* state = &app;

  WidgetRef menu = Column::make({
      .mainAxisSize = MainAxisSize::Min,
      .children =
          {
              text("COUNTER-STRIKE", theme.title()),
              gap(theme.unit() * 0.5f),
              text("a fltr / raylib demonstration", theme.label()),
              gap(theme.unit() * 4.0f),
              menuButton(theme, "Find Servers",
                         [state] { state->goTo(Page::Servers); }),
              // Disabled on purpose: the demo needs one, and a menu with a dead
              // entry is exactly where a real one has them.
              menuButton(theme, "Create Server", Callback<void()>{}, false),
              menuButton(theme, "Options", [state] { state->goTo(Page::Settings); }),
              gap(theme.unit() * 1.5f),
              menuButton(theme, "Quit", [state] { state->requestQuit(); }),
          },
  });

  return Stack::make({
      .fit = StackFit::Expand,
      .children =
          {
              // Centred at any window size, because nothing here is a position:
              // it is an alignment over a column that shrink-wraps.
              Align::make({.alignment = Alignment::center(), .child = menu}),
              Positioned::make({
                  .left = theme.unit() * 2.0f,
                  .bottom = theme.unit() * 1.5f,
                  .child = text(kVersion, theme.label()),
              }),
          },
  });
}

}  // namespace fltrdemo
