#include "app/pages.hpp"

#include <optional>

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "ui/button.hpp"
#include "ui/controls.hpp"
#include "ui/layout.hpp"
#include "ui/panel.hpp"
#include "ui/scroll.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

namespace {

constexpr std::string_view kTabs[] = {"Internet", "Favourites", "LAN"};

/// The column layout, in theme units, shared by the header and every row --
/// which is the only reason the two line up at any window size.
struct Columns {
  float map = 0.0f;
  float players = 0.0f;
  float ping = 0.0f;
  float vac = 0.0f;
};

Columns columnsOf(const Theme& theme) {
  return {.map = theme.unit() * 14.0f,
          .players = theme.unit() * 10.0f,
          .ping = theme.unit() * 8.0f,
          .vac = theme.unit() * 7.0f};
}

WidgetRef cell(float width, WidgetRef child) {
  return ConstrainedBox::make({
      .constraints = BoxConstraints::tightWidth(width),
      .child = Align::make({
          .alignment = Alignment::centerLeft(),
          .heightFactor = 1.0f,
          .child = child,
      }),
  });
}

// ---------------------------------------------------------------------------
// A row
// ---------------------------------------------------------------------------

/// One server.
///
/// Keyed by the server's id, which is what makes a re-sort a permutation rather
/// than a rewrite: the element, its `State` and its render object move, and only
/// rows that genuinely arrived are built from nothing. The fade below is how
/// that is visible rather than merely claimed -- after a sort nothing fades, and
/// after a refresh only the new rows do.
class ServerRow final : public Configure<ServerRow, StatefulWidget> {
public:
  struct Args {
    Key key;
    const ServerInfo* info = nullptr;
    AppState* app = nullptr;
  };

  explicit ServerRow(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.info != nullptr && args.app != nullptr, "a ServerRow needs a server");
  }

  const char* name() const noexcept override { return "ServerRow"; }
  const ServerInfo& info() const noexcept { return *args_.info; }
  AppState& app() const noexcept { return *args_.app; }

  std::unique_ptr<State<ServerRow>> createState() const;

private:
  Args args_;
};

class ServerRowState final : public State<ServerRow> {
public:
  void initState() override {
    driver_.attach(context().tickers());
    driver_.setConfig({.duration = 0.20f, .curve = Curves::easeOut});
    driver_.forward();
    subscribeMember<ServerRowState, &ServerRowState::onSelectionChanged>(
        widget().app().servers().selection(), subscription_, this);
    selected_ = isSelected();
  }

  void dispose() override {
    subscription_.detach();
    driver_.detach();
  }

  WidgetRef build(BuildContext& context) override {
    // A State that owns a driver has to opt into TickerMode itself, exactly as
    // the library's implicit widgets do: reading it here is both the mute and
    // the dependency that rebuilds this row when the page is hidden. Without
    // this line the fade below would keep asking for frames from behind a page
    // nobody is looking at.
    driver_.setMuted(!TickerMode::of(context));

    const Theme& theme = themeOf(context);
    const Columns columns = columnsOf(theme);
    const ServerInfo& server = widget().info();
    FrameStrings& strings = widget().app().strings();

    TextStyle style = theme.row();
    style.color = selected_ ? theme.textBright : hovered_ ? theme.text : theme.textDim;
    TextStyle dim = style;
    dim.color = selected_ ? theme.textBright : theme.textDim;
    TextStyle ping = style;
    ping.color = server.ping < 60 ? theme.good : server.ping < 120 ? theme.text : theme.danger;

    WidgetRef cells = Row::make({
        .crossAxisAlignment = CrossAxisAlignment::Stretch,
        .children =
            {
                // The name takes what is left, so a narrow window shortens the
                // name and never the numbers.
                Flexible::make({
                    .child = Align::make({.alignment = Alignment::centerLeft(),
                                          .heightFactor = 1.0f,
                                          .child = text(server.name, style)}),
                }),
                cell(columns.map, text(server.map, dim)),
                // Numbers go through the frame's arena. The render object keeps
                // a copy of what it is given, so a formatted view only has to
                // survive the build that mentions it -- which is exactly what
                // the arena guarantees.
                cell(columns.players,
                     text(strings.format("%d/%d", server.players, server.maxPlayers), dim)),
                cell(columns.ping, text(strings.format("%d", server.ping), ping)),
                cell(columns.vac, text(server.vac ? "yes" : "-", dim)),
            },
    });

    return RepaintBoundary::make({
        .child = Opacity::make({
            .opacity = 1.0f,
            // Render-attached, so a row fading in is a repaint of one boundary
            // and no build at all.
            .animation = &fade_,
            .child = Pointer::make({
                .behavior = HitTestBehavior::Opaque,
                .onEnter = Callback<void()>([this] { setHovered(true); }),
                .onExit = Callback<void()>([this] { setHovered(false); }),
                .onTap = Callback<void()>(
                    [this] { widget().app().servers().select(widget().info().id); }),
                .child = SizedBox::make({
                    .size = {kInf, theme.rowHeight()},
                    .child = DecoratedBox::make({
                        .decoration = {.color = selected_  ? theme.selection
                                                : hovered_ ? theme.surfaceRaised
                                                           : Color::transparent()},
                        .child = Padding::make({
                            .padding = EdgeInsets::symmetric(theme.unit(), 0.0f),
                            .child = cells,
                        }),
                    }),
                }),
            }),
        }),
    });
  }

private:
  bool isSelected() const noexcept {
    return widget().app().servers().selected() == widget().info().id;
  }

  /// Every row hears every selection change; only the two whose answer changed
  /// do anything about it.
  void onSelectionChanged() {
    if (selected_ == isSelected()) return;
    setState([this] { selected_ = isSelected(); });
  }

  void setHovered(bool hovered) {
    if (hovered_ == hovered) return;
    setState([&] { hovered_ = hovered; });
  }

  AnimationDriver driver_;
  AnimatedValue<float> fade_{driver_, {0.0f, 1.0f}};
  Subscription subscription_;
  bool hovered_ = false;
  bool selected_ = false;
};

std::unique_ptr<State<ServerRow>> ServerRow::createState() const {
  return std::make_unique<ServerRowState>();
}

// ---------------------------------------------------------------------------
// The scrollbar
// ---------------------------------------------------------------------------

/// A thumb that follows the scroll position without a single rebuild: the
/// position is mapped to an `Alignment`, and the alignment is handed to a render
/// object. Scrolling the list moves this by repainting one boundary.
class ScrollBar final : public Configure<ScrollBar, StatefulWidget> {
public:
  struct Args {
    Key key;
    ScrollController* controller = nullptr;
  };
  explicit ScrollBar(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.controller != nullptr, "a ScrollBar needs a controller");
  }
  const char* name() const noexcept override { return "ScrollBar"; }
  ScrollController& controller() const noexcept { return *args_.controller; }
  std::unique_ptr<State<ScrollBar>> createState() const;

private:
  Args args_;
};

class ScrollBarState final : public State<ScrollBar> {
public:
  void initState() override {
    thumb_.emplace(widget().controller().position(), &toAlignment, &widget().controller());
  }

  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    // The extents are a layout result rather than a notification, so the derived
    // value is recomputed here: a build of this subtree is the signal that the
    // list just changed shape.
    thumb_->recompute();

    return SizedBox::make({
        .size = {theme.unit(), kInf},
        .child = Stack::make({
            .fit = StackFit::Expand,
            .children =
                {
                    DecoratedBox::make({.decoration = {.color = theme.surfaceSunken}}),
                    Align::make({
                        .alignment = thumb_->value(),
                        .animation = &*thumb_,
                        .child = SizedBox::make({
                            .size = {theme.unit(), theme.unit() * 5.0f},
                            .child = DecoratedBox::make({
                                .decoration = {.color = theme.borderBright,
                                               .radius = BorderRadius::all(theme.radius())},
                            }),
                        }),
                    }),
                },
        }),
    });
  }

private:
  static Alignment toAlignment(const float&, void* context) {
    auto* controller = static_cast<ScrollController*>(context);
    return Alignment{0.0f, -1.0f + 2.0f * controller->fraction()};
  }

  std::optional<Mapped<float, Alignment>> thumb_;
};

std::unique_ptr<State<ScrollBar>> ScrollBar::createState() const {
  return std::make_unique<ScrollBarState>();
}

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------

WidgetRef headerCell(AppState& app, std::string_view label, ServerColumn column, float width) {
  ServerBrowser& browser = app.servers();
  const bool sorted = browser.sortColumn() == column;
  AppState* state = &app;

  WidgetRef button = Button::make({
      .label = sorted ? app.strings().format("%.*s %s", static_cast<int>(label.size()),
                                             label.data(), browser.ascending() ? "^" : "v")
                      : label,
      .onPressed = Callback<void()>([state, column] { state->servers().sortBy(column); }),
      .selected = sorted,
      .kind = ButtonKind::Tab,
      .width = width,
  });
  // The name column takes whatever the numbers leave, and its label sits over
  // the names rather than over the middle of the space they were given.
  return width > 0.0f ? button
                      : Flexible::make({
                            .child = Align::make({.alignment = Alignment::centerLeft(),
                                                  .heightFactor = 1.0f,
                                                  .child = button}),
                        });
}

WidgetRef header(const Theme& theme, AppState& app) {
  const Columns columns = columnsOf(theme);
  return Row::make({
      .children =
          {
              headerCell(app, "Server", ServerColumn::Name, 0.0f),
              headerCell(app, "Map", ServerColumn::Map, columns.map),
              headerCell(app, "Players", ServerColumn::Players, columns.players),
              headerCell(app, "Ping", ServerColumn::Ping, columns.ping),
              headerCell(app, "VAC", ServerColumn::Vac, columns.vac),
          },
  });
}

WidgetRef list(const Theme& theme, AppState& app) {
  ServerBrowser& browser = app.servers();
  AppState* state = &app;
  const std::span<const ServerInfo* const> servers = browser.visible();

  return Row::make({
      .crossAxisAlignment = CrossAxisAlignment::Stretch,
      .children =
          {
              Flexible::make({
                  .child = ScrollPanel::make({
                      .controller = &browser.scroll(),
                      .child = Column::make({
                          .crossAxisAlignment = CrossAxisAlignment::Stretch,
                          .mainAxisSize = MainAxisSize::Min,
                          // A couple of hundred rows, built eagerly, keyed by
                          // server id.
                          .children = WidgetList::generate(
                              servers.size(),
                              [servers, state](std::size_t i) {
                                const ServerInfo* server = servers[i];
                                return ServerRow::make({
                                    .key = Key::of(static_cast<int>(server->id)),
                                    .info = server,
                                    .app = state,
                                });
                              }),
                      }),
                  }),
              }),
              gap(theme.unit() * 0.5f),
              ScrollBar::make({.controller = &browser.scroll()}),
          },
  });
}

WidgetRef status(const Theme& theme, AppState& app) {
  ServerBrowser& browser = app.servers();
  AppState* state = &app;
  const ServerInfo* selected = browser.selectedServer();

  return Row::make({
      .crossAxisAlignment = CrossAxisAlignment::Center,
      .spacing = theme.unit(),
      .children =
          {
              Flexible::make({
                  .child = text(app.strings().format("%zu servers   %d players%s%s",
                                                     browser.visible().size(),
                                                     browser.visiblePlayers(),
                                                     selected ? "   -   " : "",
                                                     selected ? selected->name.c_str() : ""),
                                theme.label()),
              }),
              Button::make({
                  .label = "Refresh",
                  .onPressed = Callback<void()>([state] { state->servers().refresh(); }),
                  .kind = ButtonKind::Compact,
              }),
              Button::make({
                  .label = "Connect",
                  // There is nothing to connect to, so it does what a demo can.
                  // Disabled until something is selected, which is the other
                  // thing a disabled button is for.
                  .onPressed = Callback<void()>([state] { state->goBack(); }),
                  .enabled = selected != nullptr,
                  .kind = ButtonKind::Compact,
              }),
              Button::make({
                  .label = "Back",
                  .onPressed = Callback<void()>([state] { state->goBack(); }),
                  .kind = ButtonKind::Compact,
              }),
          },
  });
}

}  // namespace

WidgetRef buildServersPage(BuildContext& context, AppState& app) {
  const Theme& theme = themeOf(context);
  AppState* state = &app;

  // One Watch over the browser's revision covers the tabs, the header, the list
  // and the status line -- everything whose shape depends on the view. Hovering
  // a row, selecting one and scrolling all happen below it, and none of them
  // reach it.
  WidgetRef body = Watch<int>::make({
      .value = &app.servers().revision(),
      .builder =
          [state](BuildContext& inner, const int&) {
            const Theme& t = themeOf(inner);
            ServerBrowser& browser = state->servers();
            return Column::make({
                .crossAxisAlignment = CrossAxisAlignment::Stretch,
                .children =
                    {
                        TabStrip::make({
                            .labels = kTabs,
                            .count = std::size(kTabs),
                            .selected = static_cast<int>(browser.tab()),
                            .onSelected = Callback<void(int)>([state](int next) {
                              state->servers().setTab(static_cast<ServerTab>(next));
                            }),
                        }),
                        gap(t.unit() * 0.75f),
                        header(t, *state),
                        divider(t),
                        Flexible::make({.child = list(t, *state)}),
                        divider(t),
                        gap(t.unit() * 0.75f),
                        status(t, *state),
                    },
            });
          },
  });

  return Padding::make({
      .padding = EdgeInsets::all(theme.unit() * 3.0f),
      .child = Panel::make({.title = "SERVER BROWSER", .child = body}),
  });
}

}  // namespace fltrdemo
