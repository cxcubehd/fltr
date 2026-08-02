#pragma once

#include <string_view>

#include "fltr/core/callback.hpp"
#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"

namespace demo {

struct MenuButtonSpec {
  fltr::Key key;
  std::string_view label;
  /// Right-aligned secondary text: a shortcut hint, a value, a lock reason.
  std::string_view trailing;
  fltr::Callback<void()> onPressed;
  bool enabled = true;
  bool selected = false;
  bool autofocus = false;
};

/// Behaviour from `RawButton`, appearance from `StyledSurface`, and whatever
/// body the caller wants between them. The two never meet -- the button
/// publishes states and the surface reads them through the ambient
/// `ComponentScope`, so hovering one rebuilds nothing.
fltr::WidgetRef pressable(const Theme& theme, const MenuButtonSpec& spec, fltr::WidgetRef body);

/// A slim menu row: a label, and a shortcut hint pushed to the right.
fltr::WidgetRef menuButton(const Theme& theme, const MenuButtonSpec& spec);

/// The same behaviour with a taller, two-line body, used by level select.
fltr::WidgetRef listCard(const Theme& theme, const MenuButtonSpec& spec, std::string_view blurb);

/// The header control every screen below the main menu carries: the same action
/// Escape performs, so the way back is visible and not only remembered. Quiet
/// on purpose -- it sits beside a title, not among the choices.
fltr::WidgetRef backButton(const Theme& theme, fltr::Callback<void()> onPressed);

}  // namespace demo
