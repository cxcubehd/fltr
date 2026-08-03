#include "ui/screens/settings.hh"

#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "app/app.hh"
#include "ui/widgets/menu_button.hh"
#include "ui/widgets/panel.hh"
#include "ui/widgets/slider_row.hh"
#include "ui/widgets/toggle_row.hh"

namespace demo {

using fltr::BuildContext;
using fltr::Column;
using fltr::CrossAxisAlignment;
using fltr::EdgeInsets;
using fltr::Key;
using fltr::MainAxisSize;
using fltr::Row;
using fltr::ToggleValue;
using fltr::Watch;
using fltr::WidgetRef;

namespace {

WidgetRef graphicsPanel(App& app, const Theme& theme, const GraphicsSettings& current) {
  return panel(
      theme, EdgeInsets::all(theme.unit * 0.75f),
      Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Stretch,
          .mainAxisSize = MainAxisSize::Min,
          .children =
              {
                  toggleRow(theme,
                            {
                                .key = Key::of("settings.fullscreen"),
                                .label = "Fullscreen",
                                .hint = current.fullscreen ? "ON" : "OFF",
                                .value = current.fullscreen,
                                .onChanged =
                                    [&app](ToggleValue value) {
                                      GraphicsSettings next = app.graphics().value();
                                      next.fullscreen = value == ToggleValue::On;
                                      app.applyGraphics(next);
                                    },
                                .autofocus = true,
                            }),
                  divider(theme),
                  toggleRow(theme,
                            {
                                .key = Key::of("settings.vsync"),
                                .label = "Vertical sync",
                                .hint = current.vsync ? "ON" : "OFF",
                                .value = current.vsync,
                                .onChanged =
                                    [&app](ToggleValue value) {
                                      GraphicsSettings next = app.graphics().value();
                                      next.vsync = value == ToggleValue::On;
                                      app.applyGraphics(next);
                                    },
                            }),
                  divider(theme),
                  sliderRow(theme,
                            {
                                .key = Key::of("settings.fps"),
                                .label = "Frame cap",
                                .valueText = app.maxFpsText(),
                                .value = static_cast<float>(current.maxFps),
                                .min = 30.0f,
                                .max = 240.0f,
                                // Seven divisions is eight stops, 30 apart, and
                                // the component snaps for us -- the visual never
                                // sees an intermediate value.
                                .divisions = 7,
                                .onChanged =
                                    [&app](float value) {
                                      GraphicsSettings next = app.graphics().value();
                                      next.maxFps = static_cast<int>(value + 0.5f);
                                      app.applyGraphics(next);
                                    },
                                // The cap does nothing while vsync is driving
                                // the frame rate, so it says so by being off.
                                .enabled = !current.vsync,
                            }),
              },
      }));
}

/// Not wrapped in a `Watch`: the scale rebuilds the whole tree by definition, so
/// this row is rebuilt with a fresh theme every time it moves.
WidgetRef interfacePanel(App& app, const Theme& theme) {
  return panel(theme, EdgeInsets::all(theme.unit * 0.75f),
               sliderRow(theme, {
                                    .key = Key::of("settings.scale"),
                                    .label = "Interface scale",
                                    .valueText = app.uiScaleText(),
                                    .value = app.uiScale().value(),
                                    .min = 0.75f,
                                    .max = 2.0f,
                                    // Six stops, a quarter apart.
                                    .divisions = 5,
                                    .onChanged = [&app](float value) { app.setUiScale(value); },
                                }));
}

}  // namespace

WidgetRef settingsScreen(App& app, const Theme& theme) {
  return sheet(
      Key::of("screen.settings"), theme, theme.px(560.0f),
      Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Stretch,
          .mainAxisSize = MainAxisSize::Min,
          .spacing = theme.unit * 1.5f,
          .children =
              {
                  Row::make({
                      .children = {title(theme, "Settings"), spacer(),
                                   backButton(theme, [&app] { app.back(); })},
                  }),
                  divider(theme),
                  // Only this subtree is rebuilt when a setting changes: the
                  // title, the rule and the frame around it are untouched.
                  Watch<GraphicsSettings>::make({
                      .value = &app.graphics(),
                      .builder = [&app](BuildContext& context,
                                        const GraphicsSettings& current) -> WidgetRef {
                        return graphicsPanel(app, ThemeScope::of(context), current);
                      },
                  }),
                  interfacePanel(app, theme),
                  label(theme, "THE FRAME CAP IS APPLIED TO THE WINDOW, NOT TO THE UI: FLTR "
                               "DOES NO WORK IN A FRAME WHERE NOTHING CHANGED. THE INTERFACE "
                               "SCALE IS APPLIED TO THE SIZES THEMSELVES, SO TEXT IS "
                               "RASTERISED AT THE SIZE IT IS DRAWN AT."),
              },
      }));
}

}  // namespace demo
