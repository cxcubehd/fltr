#include "app/pages.hpp"

#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "ui/button.hpp"
#include "ui/controls.hpp"
#include "ui/layout.hpp"
#include "ui/menu.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

namespace {

// Static storage, because `Text` does not copy what it is given: a label handed
// to a widget has to outlive the build that mentioned it, and these outlive
// everything.
constexpr std::string_view kTabs[] = {"Video", "Audio", "Controls"};
constexpr std::string_view kCrosshairs[] = {"Cross", "Dot", "Circle", "None"};

/// A slider row, described rather than written out.
///
/// The description exists because of a real budget: a `Callback` stores four
/// pointers of trivially copyable capture, and a builder that captured a label,
/// a range, a format and two pointers would not fit. Capturing one pointer to a
/// constant description and one to the app fits in half of it, and the rows
/// stop being copy-paste at the same time.
struct SliderSpec {
  std::string_view label;
  Observable<float> Settings::*field;
  float min = 0.0f;
  float max = 1.0f;
  const char* format = "%.2f";
  /// What the readout multiplies the value by, for a setting shown as a
  /// percentage of a range that is not one.
  float display = 1.0f;
  /// Shown under the label, for the one setting whose effect is deliberately
  /// somewhere else entirely.
  std::string_view note;
};

constexpr SliderSpec kBrightness{.label = "Brightness",
                                 .field = &Settings::brightness,
                                 .format = "%.2f",
                                 .note = "affects the whole application"};
constexpr SliderSpec kVolume{.label = "Master volume",
                             .field = &Settings::masterVolume,
                             .format = "%.0f%%",
                             .display = 100.0f};
constexpr SliderSpec kSensitivity{.label = "Mouse sensitivity",
                                  .field = &Settings::sensitivity,
                                  .min = 0.1f,
                                  .max = 10.0f,
                                  .format = "%.2f"};

struct ToggleSpec {
  std::string_view label;
  Observable<bool> Settings::*field;
};

constexpr ToggleSpec kVsync{"Vertical sync", &Settings::vsync};
constexpr ToggleSpec kInvert{"Invert mouse", &Settings::invertMouse};
constexpr ToggleSpec kRawInput{"Raw input", &Settings::rawInput};

/// Label on the left, control on the right, at every window size: the label
/// column is a fraction of the theme rather than a pixel count.
WidgetRef settingRow(const Theme& theme, std::string_view label, WidgetRef control,
                     std::string_view note = {}) {
  WidgetRef caption = note.empty()
                          ? text(label, theme.body())
                          : Column::make({
                                .crossAxisAlignment = CrossAxisAlignment::Start,
                                .mainAxisSize = MainAxisSize::Min,
                                .children = {text(label, theme.body()), text(note, theme.label())},
                            });

  return Padding::make({
      .padding = EdgeInsets::symmetric(0.0f, theme.unit() * 0.75f),
      .child = Row::make({
          .crossAxisAlignment = CrossAxisAlignment::Center,
          .children =
              {
                  ConstrainedBox::make({
                      .constraints = BoxConstraints::tightWidth(theme.unit() * 26.0f),
                      .child = caption,
                  }),
                  control,
              },
      }),
  });
}

WidgetRef sliderRow(BuildContext& context, AppState& app, const SliderSpec& spec) {
  const Theme& theme = themeOf(context);
  AppState* state = &app;
  const SliderSpec* described = &spec;

  return settingRow(
      theme, spec.label,
      // Only this row rebuilds when the value moves -- not the panel, not the
      // page, and not the other rows. The readout is a formatted string, so the
      // rebuild is what keeps it true.
      Watch<float>::make({
          .value = &(app.settings.*spec.field),
          .builder =
              [state, described](BuildContext& inner, const float& value) {
                const Theme& t = themeOf(inner);
                return Row::make({
                    .crossAxisAlignment = CrossAxisAlignment::Center,
                    .children =
                        {
                            Slider::make({
                                .value = value,
                                .min = described->min,
                                .max = described->max,
                                .onChanged = Callback<void(float)>([state,
                                                                    described](float next) {
                                  (state->settings.*described->field).set(next);
                                }),
                                .router = &state->router(),
                            }),
                            gap(t.unit()),
                            // Fixed width, so the row does not twitch as the
                            // number's digits change.
                            ConstrainedBox::make({
                                .constraints = BoxConstraints::tightWidth(t.unit() * 6.0f),
                                // Formatted into the frame's arena, which is
                                // reset at the top of the next frame -- and
                                // handed only to a `Text` built in this one.
                                .child = text(state->strings().format(
                                                  described->format,
                                                  static_cast<double>(value * described->display)),
                                              t.body()),
                            }),
                        },
                });
              },
      }),
      spec.note);
}

WidgetRef toggleRow(BuildContext& context, AppState& app, const ToggleSpec& spec) {
  const Theme& theme = themeOf(context);
  AppState* state = &app;
  Observable<bool> Settings::*field = spec.field;

  return settingRow(theme, spec.label,
                    Watch<bool>::make({
                        .value = &(app.settings.*field),
                        .builder =
                            [state, field](BuildContext&, const bool& value) {
                              return Toggle::make({
                                  .value = value,
                                  .onChanged = Callback<void(bool)>([state, field](bool next) {
                                    (state->settings.*field).set(next);
                                  }),
                              });
                            },
                    }));
}

WidgetRef crosshairRow(BuildContext& context, AppState& app) {
  const Theme& theme = themeOf(context);
  AppState* state = &app;

  return settingRow(theme, "Crosshair",
                    Watch<int>::make({
                        .value = &app.settings.crosshair,
                        .builder =
                            [state](BuildContext&, const int& value) {
                              return Dropdown::make({
                                  .id = 1,
                                  .options = kCrosshairs,
                                  .count = std::size(kCrosshairs),
                                  .value = value,
                                  .onChanged = Callback<void(int)>(
                                      [state](int next) { state->settings.crosshair.set(next); }),
                                  .controller = &state->menus(),
                              });
                            },
                    }));
}

WidgetRef tabContent(BuildContext& context, AppState& app, int tab) {
  switch (tab) {
    case 0:
      return Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Start,
          .mainAxisSize = MainAxisSize::Min,
          .children =
              {
                  sliderRow(context, app, kBrightness),
                  toggleRow(context, app, kVsync),
                  crosshairRow(context, app),
              },
      });
    case 1:
      return Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Start,
          .mainAxisSize = MainAxisSize::Min,
          .children = {sliderRow(context, app, kVolume)},
      });
    default:
      return Column::make({
          .crossAxisAlignment = CrossAxisAlignment::Start,
          .mainAxisSize = MainAxisSize::Min,
          .children =
              {
                  sliderRow(context, app, kSensitivity),
                  toggleRow(context, app, kInvert),
                  toggleRow(context, app, kRawInput),
              },
      });
  }
}

}  // namespace

WidgetRef buildSettingsPage(BuildContext& context, AppState& app) {
  const Theme& theme = themeOf(context);
  AppState* state = &app;

  WidgetRef body = Column::make({
      .crossAxisAlignment = CrossAxisAlignment::Stretch,
      .children =
          {
              // The tab strip and the tab's contents watch the same value and
              // are two separate rebuilds. Neither one rebuilds the panel
              // around them.
              Watch<int>::make({
                  .value = &app.settings.tab,
                  .builder =
                      [state](BuildContext&, const int& tab) {
                        return TabStrip::make({
                            .labels = kTabs,
                            .count = std::size(kTabs),
                            .selected = tab,
                            .onSelected = Callback<void(int)>(
                                [state](int next) { state->settings.tab.set(next); }),
                        });
                      },
              }),
              gap(theme.unit() * 1.5f),
              Flexible::make({
                  .child = Align::make({
                      .alignment = Alignment::topLeft(),
                      .child = Watch<int>::make({
                          .value = &app.settings.tab,
                          .builder =
                              [state](BuildContext& inner, const int& tab) {
                                return tabContent(inner, *state, tab);
                              },
                      }),
                  }),
              }),
              divider(theme),
              gap(theme.unit()),
              Row::make({
                  .mainAxisAlignment = MainAxisAlignment::End,
                  .spacing = theme.unit(),
                  .children =
                      {
                          // Apply is deliberately a no-op: every control here is
                          // live, which is the point being made.
                          Button::make({.label = "Apply",
                                        .onPressed = Callback<void()>([] {}),
                                        .kind = ButtonKind::Compact}),
                          Button::make({.label = "Back",
                                        .onPressed =
                                            Callback<void()>([state] { state->goBack(); }),
                                        .kind = ButtonKind::Compact}),
                      },
              }),
          },
  });

  // The host is outside the padding, so a menu may open over anything on the
  // page -- including the frame around it.
  return MenuHost::make({
      .controller = &app.menus(),
      .child = Padding::make({
          .padding = EdgeInsets::all(theme.unit() * 3.0f),
          .child = Panel::make({.title = "OPTIONS", .child = body}),
      }),
  });
}

}  // namespace fltrdemo
