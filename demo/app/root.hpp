#pragma once

#include <memory>

#include "app/app_state.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

/// The root of the demo's tree, and the only widget `main` names.
///
/// Above the pages it does three things, each of which is one of the
/// framework's mechanisms doing its job:
///
///   - `Watch<Size>` over the surface derives the theme, so a resize rebuilds
///     the ambient value and nothing else;
///   - `Ambient<Theme>` publishes it, so a scale change rebuilds exactly the
///     elements whose last build read the theme;
///   - a `Watch<float>` over the brightness setting paints a scrim over
///     everything -- the demonstration that a setting changes something outside
///     the panel that changed it, scoped to a subtree rather than to a page.
class DemoApp final : public fltr::Configure<DemoApp, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    AppState* state = nullptr;
  };

  explicit DemoApp(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.state != nullptr, "the demo root needs its state");
  }

  const char* name() const noexcept override { return "DemoApp"; }
  AppState& state() const noexcept { return *args_.state; }

  std::unique_ptr<fltr::State<DemoApp>> createState() const;

private:
  Args args_;
};

}  // namespace fltrdemo
