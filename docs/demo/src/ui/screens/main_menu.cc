#include "ui/screens/main_menu.hh"

#include "fltr/widgets/basic.hpp"
#include "app/app.hh"
#include "ui/widgets/menu_button.hh"
#include "ui/widgets/panel.hh"

namespace demo {

using fltr::Column;
using fltr::CrossAxisAlignment;
using fltr::EdgeInsets;
using fltr::Key;
using fltr::MainAxisSize;
using fltr::Row;
using fltr::WidgetRef;

WidgetRef mainMenuScreen(App& app, const Theme& theme) {
  return sheet(
      Key::of("screen.menu"), theme, 460.0f,
      Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Stretch,
          .mainAxisSize = MainAxisSize::Min,
          .spacing = theme.unit * 2.0f,
          .children =
              {
                  Column::make({
                      .crossAxisAlignment = CrossAxisAlignment::Start,
                      .mainAxisSize = MainAxisSize::Min,
                      .spacing = theme.unit * 0.5f,
                      .children =
                          {
                              display(theme, "DRIFT"),
                              label(theme, "AN ASTEROID FIELD, A FLOATY SHIP, AND A USER "
                                           "INTERFACE"),
                          },
                  }),
                  panel(theme, EdgeInsets::all(theme.unit * 0.75f),
                        Column::make({
                            .crossAxisAlignment = CrossAxisAlignment::Stretch,
                            .mainAxisSize = MainAxisSize::Min,
                            .spacing = theme.unit * 0.5f,
                            .children =
                                {
                                    menuButton(theme,
                                               {
                                                   .key = Key::of("menu.launch"),
                                                   .label = "Launch",
                                                   .trailing = "ENTER",
                                                   // One captured pointer: a
                                                   // `Callback` stores 32 bytes
                                                   // inline and refuses more.
                                                   .onPressed = [&app] {
                                                     app.goTo(Screen::LevelSelect);
                                                   },
                                                   .autofocus = true,
                                               }),
                                    menuButton(theme,
                                               {
                                                   .key = Key::of("menu.settings"),
                                                   .label = "Settings",
                                                   .trailing = "S",
                                                   .onPressed = [&app] {
                                                     app.goTo(Screen::Settings);
                                                   },
                                               }),
                                    menuButton(theme,
                                               {
                                                   .key = Key::of("menu.quit"),
                                                   .label = "Quit",
                                                   .trailing = "ESC",
                                                   .onPressed = [&app] { app.quit(); },
                                               }),
                                },
                        })),
                  Row::make({
                      .children =
                          {
                              label(theme, "BEST"),
                              spacer(),
                              body(theme, app.session().bestText()),
                          },
                  }),
              },
      }));
}

}  // namespace demo
