#include "ui/panel.hpp"

#include "fltr/widgets/basic.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

WidgetRef Panel::build(BuildContext& context) const {
  const Theme& theme = themeOf(context);

  // A parent-data widget must have a child to configure, so an empty panel gets
  // an empty box rather than a null ref.
  WidgetRef body = args_.child ? args_.child : SizedBox::make({});
  if (args_.fill) body = Flexible::make({.child = body});

  const bool titled = !args_.title.empty();
  return DecoratedBox::make({
      .decoration = theme.panel(),
      .child = Padding::make({
          .padding = EdgeInsets::all(theme.unit()),
          .child = Column::make({
              .crossAxisAlignment = CrossAxisAlignment::Stretch,
              .mainAxisSize = args_.fill ? MainAxisSize::Max : MainAxisSize::Min,
              .children =
                  {
                      titled ? Padding::make({
                                   .padding = EdgeInsets::only(theme.unit() * 0.5f, 0.0f, 0.0f,
                                                               theme.unit()),
                                   .child = text(args_.title, theme.heading()),
                               })
                             : WidgetRef{},
                      titled ? divider(theme) : WidgetRef{},
                      titled ? gap(theme.unit()) : WidgetRef{},
                      body,
                  },
          }),
      }),
  });
}

}  // namespace fltrdemo
