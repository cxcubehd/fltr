#include "ui/screens/pause.hh"

#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "fltr/widgets/switcher.hpp"
#include "app/app.hh"
#include "ui/router.hh"
#include "ui/theme.hh"
#include "ui/widgets/menu_button.hh"
#include "ui/widgets/panel.hh"

namespace demo {

using fltr::AnimatedSwitcher;
using fltr::BuildContext;
using fltr::Column;
using fltr::CrossAxisAlignment;
using fltr::DecoratedBox;
using fltr::EdgeInsets;
using fltr::Key;
using fltr::MainAxisSize;
using fltr::Row;
using fltr::Stack;
using fltr::StackFit;
using fltr::Watch;
using fltr::WidgetRef;

namespace {

std::string_view headlineFor(Outcome outcome) {
  switch (outcome) {
    case Outcome::Cleared: return "Field cleared";
    case Outcome::Destroyed: return "Hull breached";
    default: return "Paused";
  }
}

WidgetRef panelFor(App& app, const Theme& theme, Outcome outcome) {
  const bool over = outcome != Outcome::Flying;

  return Stack::make({
      .fit = StackFit::Expand,
      .children =
          {
              // The scrim is part of the panel rather than a separate entry, so
              // it fades with it: one subtree in, one subtree out.
              DecoratedBox::make({.decoration = scrimDecoration(theme)}),
              sheet(Key::of("pause.sheet"), theme, 420.0f,
                    panel(theme, EdgeInsets::all(theme.unit * 0.75f),
                          Column::make({
                              .crossAxisAlignment = CrossAxisAlignment::Stretch,
                              .mainAxisSize = MainAxisSize::Min,
                              .spacing = theme.unit,
                              .children =
                                  {
                                      bare(EdgeInsets::symmetric(theme.unit * 1.75f,
                                                                 theme.unit * 1.25f),
                                           Row::make({
                                               .children = {title(theme, headlineFor(outcome)),
                                                            spacer(),
                                                            label(theme, app.session().scoreText())},
                                           })),
                                      divider(theme),
                                      menuButton(theme,
                                                 {
                                                     .key = Key::of("pause.resume"),
                                                     .label = over ? "Fly again" : "Resume",
                                                     .trailing = over ? "ENTER" : "ESC",
                                                     .onPressed =
                                                         [&app] {
                                                           if (app.session().outcome.value() !=
                                                               Outcome::Flying) {
                                                             app.startLevel(
                                                                 app.session().level());
                                                           } else {
                                                             app.setPaused(false);
                                                           }
                                                         },
                                                     .autofocus = true,
                                                 }),
                                      menuButton(theme,
                                                 {
                                                     .key = Key::of("pause.levels"),
                                                     .label = "Choose another field",
                                                     .onPressed =
                                                         [&app] {
                                                           app.abandonRun();
                                                           app.goTo(Screen::LevelSelect);
                                                         },
                                                 }),
                                      menuButton(theme,
                                                 {
                                                     .key = Key::of("pause.menu"),
                                                     .label = "Main menu",
                                                     .onPressed =
                                                         [&app] {
                                                           app.abandonRun();
                                                           app.goTo(Screen::MainMenu);
                                                         },
                                                 }),
                                  },
                          }))),
          },
  });
}

}  // namespace

WidgetRef pauseOverlay(App& app, BuildContext&) {
  return Watch<bool>::make({
      .value = &app.paused(),
      .builder = [&app](BuildContext& inner, const bool& paused) -> WidgetRef {
        const Theme& current = ThemeScope::of(inner);
        const Outcome outcome = app.session().outcome.value();

        // A null child is a child: the switcher animates whatever is there out
        // and leaves nothing behind. That is the M14 machinery -- the outgoing
        // element stays mounted and ticking, and is never rebuilt, until its
        // animation reaches zero.
        return AnimatedSwitcher::make({
            .child = paused ? panelFor(app, current, outcome) : WidgetRef{},
            .animation = {.duration = 0.18f, .curve = fltr::Curves::easeOut},
            .transition = &slidePresence,
        });
      },
  });
}

}  // namespace demo
