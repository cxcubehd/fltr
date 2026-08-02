#pragma once

#include <string_view>

#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"

namespace demo {

/// The small compositions every screen repeats.
///
/// fltr's catalogue is deliberately primitive -- there is no `Container`, no
/// `Spacer`, no `ColoredBox` -- so a consumer writes the handful of
/// compositions it actually uses. These are those, and they are functions
/// rather than widgets because none of them needs state or identity.

/// Padding + a decorated surface, which is the `Container` fltr does not have.
fltr::WidgetRef panel(const Theme& theme, fltr::EdgeInsets padding, fltr::WidgetRef child);

/// A panel with no fill, for grouping without adding a second surface colour.
fltr::WidgetRef bare(fltr::EdgeInsets padding, fltr::WidgetRef child);

/// Fixed empty space on both axes. `Row`/`Column` also take `spacing`, which is
/// better when the gap is uniform; this is for the one-off.
fltr::WidgetRef gap(float size);

/// Empty space that eats whatever is left, which is the `Spacer` fltr does not
/// have. Only meaningful inside a `Row` or `Column`.
fltr::WidgetRef spacer(int flex = 1);

/// A hairline rule.
fltr::WidgetRef divider(const Theme& theme);

fltr::WidgetRef title(const Theme& theme, std::string_view text);
fltr::WidgetRef display(const Theme& theme, std::string_view text);
fltr::WidgetRef body(const Theme& theme, std::string_view text);
fltr::WidgetRef label(const Theme& theme, std::string_view text);

/// A screen's outer frame: centred, with a maximum width, so a fullscreen
/// window does not stretch a menu across two metres of monitor.
fltr::WidgetRef sheet(fltr::Key key, const Theme& theme, float maxWidth,
                      fltr::WidgetRef child);

}  // namespace demo
