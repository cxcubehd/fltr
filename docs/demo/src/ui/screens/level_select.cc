#include "ui/screens/level_select.hh"

#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/scroll.hpp"
#include "app/app.hh"
#include "ui/widgets/menu_button.hh"
#include "ui/widgets/panel.hh"

namespace demo {

using fltr::Column;
using fltr::ConstrainedBox;
using fltr::CrossAxisAlignment;
using fltr::EdgeInsets;
using fltr::Key;
using fltr::MainAxisSize;
using fltr::Overscroll;
using fltr::Padding;
using fltr::Row;
using fltr::Scrollable;
using fltr::Scrollbar;
using fltr::WidgetList;
using fltr::WidgetRef;

WidgetRef levelSelectScreen(App& app, const Theme& theme) {
  const std::span<const LevelDef> table = levels();
  const Progress& progress = app.session().progress();

  WidgetList items = WidgetList::generate(table.size(), [&](std::size_t i) -> WidgetRef {
    const LevelDef& def = table[i];
    const bool unlocked = progress.unlocked(i);
    // The name is a static literal and unique, which is what a string key needs:
    // a keyed element then survives the list changing shape, as it does the
    // moment a level unlocks.
    return listCard(theme,
                    {
                        .key = Key::of(def.name),
                        .label = def.name,
                        .trailing = unlocked ? "READY" : "LOCKED",
                        .onPressed = [&app, i] { app.startLevel(i); },
                        // A disabled component still swallows the pointer, so a
                        // locked level never leaks its click to what is behind.
                        .enabled = unlocked,
                        .selected = i == app.session().level(),
                        .autofocus = i == 0,
                    },
                    unlocked ? def.blurb : "Score more to unlock.");
  });

  return sheet(
      Key::of("screen.levels"), theme, 560.0f,
      Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Stretch,
          .mainAxisSize = MainAxisSize::Min,
          .spacing = theme.unit * 1.5f,
          .children =
              {
                  Row::make({
                      .children = {title(theme, "Select a field"), spacer(),
                                   backButton(theme, [&app] { app.back(); })},
                  }),
                  divider(theme),
                  // A scrollable is non-lazy: every card is built and laid out
                  // whether or not it is on screen. A handful is fine; five
                  // thousand would not be, and that limit is documented rather
                  // than hidden.
                  ConstrainedBox::make({
                      .constraints = {.maxHeight = 360.0f},
                      .child = Scrollbar::make({
                          .controller = &app.levelScroll(),
                          .child = Overscroll::make({
                              .controller = &app.levelScroll(),
                              .child = Scrollable::make({
                                  .controller = &app.levelScroll(),
                                  .child = Padding::make({
                                      .padding = EdgeInsets::only(0.0f, 0.0f, theme.unit, 0.0f),
                                      .child = Column::make({
                                          .crossAxisAlignment = CrossAxisAlignment::Stretch,
                                          .mainAxisSize = MainAxisSize::Min,
                                          .spacing = theme.unit,
                                          .children = items,
                                      }),
                                  }),
                              }),
                          }),
                      }),
                  }),
              },
      }));
}

}  // namespace demo
