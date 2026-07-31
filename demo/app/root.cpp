#include "app/root.hpp"

#include <algorithm>

#include "app/pages.hpp"
#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

namespace {

/// Reads the theme and paints it. A widget rather than a colour computed in the
/// root's build, so the ambient read is a real one: change the scale and this
/// element rebuilds, and the elements between it and the theme do not.
class Backdrop final : public Configure<Backdrop, StatelessWidget> {
public:
  struct Args {
    Key key;
  };
  explicit Backdrop(const Args& args) : Configure(args.key) {}
  const char* name() const noexcept override { return "Backdrop"; }

  WidgetRef build(BuildContext& context) const {
    return DecoratedBox::make({.decoration = {.color = themeOf(context).background}});
  }
};

/// Brightness, as a scrim over the whole application.
///
/// It is deliberately at the root, two pages away from the slider that drives
/// it: the `Watch` rebuilds this subtree and nothing else, and the settings page
/// that changed the value does not rebuild at all.
WidgetRef buildScrim(BuildContext&, const float& brightness) {
  const float delta = brightness - 0.5f;
  const Color colour = delta < 0.0f ? Color::rgba(0, 0, 0, 0) : Color::rgba(255, 250, 235, 0);
  const auto alpha = static_cast<std::uint8_t>(std::min(1.0f, std::abs(delta) * 2.0f) *
                                               (delta < 0.0f ? 170.0f : 60.0f));
  // Always a box, never nothing: a *component* that builds a null child has no
  // render object, and a container child must have one. (A null entry in a
  // WidgetList is a different thing -- those are dropped from the list.)
  return DecoratedBox::make({.decoration = {.color = colour.withAlpha(alpha)}});
}

// ---------------------------------------------------------------------------
// PageHost
// ---------------------------------------------------------------------------

class PageHost final : public Configure<PageHost, StatefulWidget> {
public:
  struct Args {
    Key key;
    AppState* state = nullptr;
  };
  explicit PageHost(const Args& args) : Configure(args.key), args_(args) {}
  const char* name() const noexcept override { return "PageHost"; }
  AppState& state() const noexcept { return *args_.state; }
  std::unique_ptr<State<PageHost>> createState() const;

private:
  Args args_;
};

/// Page changes are animated *in*, never out.
///
/// Animating a subtree out as it is removed would need the element tree to
/// retain a dead subtree, still ticking, until its animation finished -- which
/// INSTRUCTIONS.md rules out for this phase, and for good reason. So the
/// transition is designed never to need it: every page stays mounted once it
/// has been visited, the incoming one runs a driver from zero, and the outgoing
/// one is simply not painted. Nothing is ever removed, so nothing ever has to
/// animate on its way out.
///
/// Keeping pages mounted is what `TickerMode` is for, and the decision is
/// deliberate: a page keeps its `State` and its subscriptions while hidden, and
/// costs no time at all because every ticker below it is muted.
class PageHostState final : public State<PageHost> {
public:
  void initState() override {
    driver_.attach(context().tickers());
    driver_.setConfig({.duration = 0.22f, .curve = Curves::easeOutCubic});
    // The first page is simply there; only a change animates.
    driver_.jumpTo(1.0f);
    visited_[static_cast<int>(app().page().value())] = true;
    subscribeMember<PageHostState, &PageHostState::onPageChanged>(app().page(), subscription_,
                                                                  this);
  }

  void dispose() override {
    subscription_.detach();
    driver_.detach();
  }

  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    // The slide is a theme distance, so it scales with everything else. Setting
    // the tween re-samples, which is why it is guarded: an unchanged tween must
    // not notify a render object every build.
    const float slide = theme.unit() * 3.0f;
    if (slide != slide_) {
      slide_ = slide;
      offset_.setTween({Transform2D::translation({0.0f, slide}), Transform2D::identity()});
    }

    const auto active = static_cast<int>(app().page().value());
    // The active page is last, so it is on top of the stack and its opaque
    // region takes every press: the pages beneath it are painted-out *and*
    // unreachable. Reordering keyed children is free -- they keep their
    // elements, their State and their render objects, and only their order
    // changes.
    int order[kPageCount];
    int n = 0;
    for (int page = 0; page < kPageCount; ++page) {
      if (page != active) order[n++] = page;
    }
    order[n] = active;

    return Stack::make({
        .fit = StackFit::Expand,
        .children =
            {
                wrap(context, order[0], active),
                wrap(context, order[1], active),
                wrap(context, order[2], active),
            },
    });
  }

private:
  AppState& app() const noexcept { return this->widget().state(); }

  void onPageChanged() {
    setState([this] {
      visited_[static_cast<int>(app().page().value())] = true;
      driver_.jumpTo(0.0f);
      driver_.forward();
    });
  }

  WidgetRef wrap(BuildContext& context, int page, int active) {
    const bool isActive = page == active;
    return TickerMode::make({
        .key = Key::of(page),
        // A hidden page holds no active tickers. Not "few": none -- a muted
        // ticker is not subscribed at all.
        .enabled = isActive,
        .child = Opacity::make({
            // What an inactive page is: recorded as nothing at all, because
            // PaintingContext drops a fully transparent subtree.
            .opacity = 0.0f,
            .animation = isActive ? &fade_ : nullptr,
            .child = Transform::make({
                .animation = isActive ? &offset_ : nullptr,
                .child = Pointer::make({
                    .behavior = HitTestBehavior::Opaque,
                    .child = visited_[page] ? buildPage(context, page) : WidgetRef{},
                }),
            }),
        }),
    });
  }

  WidgetRef buildPage(BuildContext& context, int page) {
    switch (static_cast<Page>(page)) {
      case Page::Title: return buildTitlePage(context, app());
      case Page::Settings: return buildSettingsPage(context, app());
      case Page::Servers: return buildServersPage(context, app());
    }
    return WidgetRef{};
  }

  AnimationDriver driver_;
  AnimatedValue<float> fade_{driver_, {0.0f, 1.0f}};
  AnimatedValue<Transform2D> offset_{driver_,
                                     {Transform2D::identity(), Transform2D::identity()}};
  Subscription subscription_;
  float slide_ = -1.0f;
  /// A page is built the first time it is visited and kept from then on. The
  /// server browser is a few thousand render objects; building it because the
  /// title screen is showing would be a strange thing to do.
  bool visited_[kPageCount] = {};
};

std::unique_ptr<State<PageHost>> PageHost::createState() const {
  return std::make_unique<PageHostState>();
}

// ---------------------------------------------------------------------------
// DemoApp
// ---------------------------------------------------------------------------

class DemoAppState final : public State<DemoApp> {
public:
  WidgetRef build(BuildContext&) override {
    AppState* app = &widget().state();
    return Watch<Size>::make({
        .value = &app->surface(),
        .builder = [app](BuildContext&, const Size& surface) {
          return ThemeScope::make({
              .value = Theme::forSurface(surface),
              .child = Stack::make({
                  .fit = StackFit::Expand,
                  .children =
                      {
                          Backdrop::make({}),
                          PageHost::make({.state = app}),
                          Watch<float>::make({
                              .value = &app->settings.brightness,
                              .builder = &buildScrim,
                          }),
                      },
              }),
          });
        },
    });
  }
};

}  // namespace

std::unique_ptr<State<DemoApp>> DemoApp::createState() const {
  return std::make_unique<DemoAppState>();
}

}  // namespace fltrdemo
