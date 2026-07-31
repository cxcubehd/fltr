#pragma once

#include <string_view>

#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

/// A framed box with an optional title bar. The boring one, kept boring.
class Panel final : public fltr::Configure<Panel, fltr::StatelessWidget> {
public:
  struct Args {
    fltr::Key key;
    std::string_view title;
    /// Whether the body takes all the height the panel was given, or only what
    /// it needs.
    bool fill = true;
    fltr::WidgetRef child;
  };

  explicit Panel(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Panel"; }
  fltr::WidgetRef build(fltr::BuildContext& context) const;

private:
  Args args_;
};

}  // namespace fltrdemo
