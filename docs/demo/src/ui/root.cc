#include "ui/root.hh"

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/focus.hpp"
#include "fltr/widgets/overlay.hpp"
#include "fltr/widgets/reactive.hpp"
#include "fltr/widgets/switcher.hpp"
#include "app/app.hh"
#include "ui/router.hh"
#include "ui/screens/hud.hh"
#include "ui/screens/level_select.hh"
#include "ui/screens/main_menu.hh"
#include "ui/screens/pause.hh"
#include "ui/screens/settings.hh"
#include "ui/theme.hh"

namespace demo {

using fltr::AnimatedSwitcher;
using fltr::BuildContext;
using fltr::FocusScope;
using fltr::Overlay;
using fltr::TickerMode;
using fltr::Watch;
using fltr::WidgetRef;

namespace {

WidgetRef screenFor(App& app, const Theme& theme, Screen screen) {
  switch (screen) {
    case Screen::MainMenu: return mainMenuScreen(app, theme);
    case Screen::LevelSelect: return levelSelectScreen(app, theme);
    case Screen::Settings: return settingsScreen(app, theme);
    case Screen::Playing: return hudScreen(app, theme);
  }
  return {};
}

/// The switcher tells presences apart by key, and two of them are alive at once
/// while a transition runs -- so the key has to name the screen, not the widget.
fltr::Key keyFor(Screen screen) {
  switch (screen) {
    case Screen::MainMenu: return fltr::Key::of("page.menu");
    case Screen::LevelSelect: return fltr::Key::of("page.levels");
    case Screen::Settings: return fltr::Key::of("page.settings");
    case Screen::Playing: return fltr::Key::of("page.playing");
  }
  return {};
}

WidgetRef themedTree(App& app, float scale) {
  return ThemeScope::make({
      // The theme carries the one thing the surfaces cannot work out for
      // themselves: whether the keyboard is what is driving the focus.
      .value = scaledTheme(scale, &app.input().keyboardMode()),
      // A focus scope is what brings the focus subsystem into existence at all:
      // a tree without one holds no focus nodes and no key handlers. It is also
      // the traversal group, so tab and the arrow keys work inside it.
      .child = FocusScope::make({
          // Nothing creates an overlay for you. One here, directly under the
          // root, is what the pause panel is inserted into.
          .child = Overlay::make({
              .child = Watch<Screen>::make({
                  .value = &app.screen(),
                  .builder = [&app](BuildContext& context, const Screen& screen) -> WidgetRef {
                    // The overlay is found here, on the way down, and kept for
                    // the callbacks that will insert into it later.
                    app.bindOverlay(Overlay::of(context));

                    return AnimatedSwitcher::make({
                        // The pause flag is watched *inside* the presence so
                        // that toggling it rebuilds one node rather than
                        // swapping the whole page.
                        .child = Watch<bool>::make({
                            .key = keyFor(screen),
                            .value = &app.paused(),
                            .builder = [&app, screen](BuildContext& inner,
                                                      const bool& paused) -> WidgetRef {
                              return TickerMode::make({
                                  // Animations in a paused gameplay screen stop
                                  // entirely: a muted ticker holds no
                                  // subscription and consumes no time.
                                  .enabled = !(screen == Screen::Playing && paused),
                                  .child = screenFor(app, ThemeScope::of(inner), screen),
                              });
                            },
                        }),
                        .animation = {.duration = 0.22f, .curve = fltr::Curves::easeOutCubic},
                        .transition = &slidePresence,
                    });
                  },
              }),
          }),
      }),
  });
}

}  // namespace

WidgetRef buildRoot(App& app) {
  // The interface scale is the one value the whole tree is measured in, so it is
  // watched at the very top: changing it is the only thing that rebuilds
  // everything, and it happens when a player drags a slider. What the player
  // asked for is a factor on top of what the display demands, so a dense screen
  // is served without the setting reading anything other than 100%.
  return Watch<float>::make({
      .value = &app.uiScale(),
      .builder = [&app](BuildContext&, const float& scale) -> WidgetRef {
        return themedTree(app, scale * app.window().contentScale());
      },
  });
}

}  // namespace demo
