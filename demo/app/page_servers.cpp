#include "app/pages.hpp"

#include "fltr/widgets/basic.hpp"
#include "ui/layout.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

WidgetRef buildServersPage(BuildContext& context, AppState&) {
  const Theme& theme = themeOf(context);
  return Padding::make({
      .padding = EdgeInsets::all(theme.unit() * 3.0f),
      .child = Panel::make({.title = "INTERNET SERVERS", .child = WidgetRef{}}),
  });
}

}  // namespace fltrdemo
