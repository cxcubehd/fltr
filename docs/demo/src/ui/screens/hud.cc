#include "ui/screens/hud.hh"

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "app/app.hh"
#include "ui/widgets/menu_button.hh"
#include "ui/widgets/panel.hh"
#include "ui/widgets/readout.hh"

namespace demo {

using fltr::AnimatedOpacity;
using fltr::BuildContext;
using fltr::Column;
using fltr::CrossAxisAlignment;
using fltr::EdgeInsets;
using fltr::IgnorePointer;
using fltr::Key;
using fltr::MainAxisSize;
using fltr::Positioned;
using fltr::RepaintBoundary;
using fltr::Row;
using fltr::Stack;
using fltr::StackFit;
using fltr::Watch;
using fltr::WidgetRef;

namespace {

std::string_view speedOf(const void* owner) {
  return static_cast<const Session*>(owner)->speedText();
}

std::string_view remainingOf(const void* owner) {
  return static_cast<const Session*>(owner)->remainingText();
}

/// Score, level name and the rolling counter.
WidgetRef scoreCluster(App& app, const Theme& theme) {
  return panel(theme, EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
               Column::make({
                   .crossAxisAlignment = CrossAxisAlignment::Start,
                   .mainAxisSize = MainAxisSize::Min,
                   .spacing = theme.unit * 0.75f,
                   .children =
                       {
                           label(theme, app.session().levelName()),
                           CountUp::make({
                               .value = &app.session().score,
                               .style = displayStyle(theme),
                           }),
                       },
               }));
}

WidgetRef statusCluster(App& app, const Theme& theme) {
  return panel(theme, EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
               Row::make({
                   .crossAxisAlignment = CrossAxisAlignment::Start,
                   .spacing = theme.unit * 2.5f,
                   .children =
                       {
                           statistic(theme, "ROCKS", &app.session().remaining, &remainingOf,
                                     &app.session()),
                           statistic(theme, "SPEED", &app.session().speed, &speedOf,
                                     &app.session()),
                       },
               }));
}

/// Built like a statistic rather than like a menu row, so it is exactly as tall
/// as the cluster beside it without either of them naming a height.
WidgetRef pauseButton(App& app, const Theme& theme) {
  return pressable(theme, {.key = Key::of("hud.pause"), .onPressed = [&app] { app.togglePause(); }},
                   fltr::Padding::make({
                       .padding = EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
                       .child = Column::make({
                           .crossAxisAlignment = CrossAxisAlignment::Start,
                           .mainAxisSize = MainAxisSize::Min,
                           .spacing = theme.unit * 0.75f,
                           .children = {label(theme, "PAUSE"), body(theme, "ESC")},
                       }),
                   }));
}

WidgetRef gauges(App& app, const Theme& theme) {
  return panel(theme, EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
               Column::make({
                   .crossAxisAlignment = CrossAxisAlignment::Stretch,
                   .mainAxisSize = MainAxisSize::Min,
                   .spacing = theme.unit,
                   .children =
                       {
                           gauge(theme, "HULL", &app.session().hullFraction, theme.danger),
                           gauge(theme, "SHIELD", &app.session().shieldFraction, theme.accent),
                       },
               }));
}

/// The one implicit animation in the demo: a configuration change starts an
/// interpolation, and the value goes down to a render object rather than being
/// snapshotted into a widget.
WidgetRef criticalWarning(App& app) {
  return Watch<float>::make({
      .value = &app.session().hullFraction,
      .builder = [](BuildContext& context, const float& hull) -> WidgetRef {
        const Theme& current = ThemeScope::of(context);
        return AnimatedOpacity::make({
            .opacity = hull < 0.35f ? 1.0f : 0.0f,
            .animation = {.duration = 0.25f, .curve = fltr::Curves::easeInOut},
            .child = panel(current, EdgeInsets::symmetric(current.unit * 1.5f, current.unit),
                           fltr::Text::make({
                               .text = "HULL CRITICAL",
                               .style = monoStyle(current, current.danger),
                           })),
        });
      },
  });
}

}  // namespace

WidgetRef hudScreen(App& app, const Theme& theme) {
  const float inset = theme.unit * 2.5f;

  // A repaint boundary around the HUD means a frame in which only the HUD moved
  // re-records the HUD's own list and leaves the root's alone.
  return RepaintBoundary::make({
      .key = Key::of("screen.hud"),
      .child = Stack::make({
          .fit = StackFit::Expand,
          .children =
              {
                  Positioned::make({
                      .left = inset,
                      .top = inset,
                      .child = scoreCluster(app, theme),
                  }),
                  Positioned::make({
                      .top = inset,
                      .right = inset,
                      .child = Row::make({
                          .mainAxisSize = MainAxisSize::Min,
                          .spacing = theme.unit,
                          .children = {statusCluster(app, theme), pauseButton(app, theme)},
                      }),
                  }),
                  Positioned::make({
                      .left = inset,
                      .bottom = inset,
                      .width = 240.0f,
                      .child = gauges(app, theme),
                  }),
                  Positioned::make({
                      .right = inset,
                      .bottom = inset,
                      // Nothing here is interactive, and an invisible warning
                      // must not eat a click that was aimed at the field.
                      .child = IgnorePointer::make({.child = criticalWarning(app)}),
                  }),
              },
      }),
  });
}

}  // namespace demo
