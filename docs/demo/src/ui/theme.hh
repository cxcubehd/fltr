#pragma once

#include "fltr/paint/text.hpp"
#include "fltr/render/boxes.hpp"
#include "fltr/widgets/reactive.hpp"

namespace demo {

/// The whole visual vocabulary of the demo, in one trivially destructible,
/// equality-comparable struct.
///
/// fltr has no theme system on purpose -- `Ambient<T>` is the mechanism, and the
/// tokens are the consumer's. Making it one struct rather than a dozen ambient
/// values means a theme change notifies once.
struct Theme {
  fltr::Color background{0x0a, 0x0a, 0x0c};
  fltr::Color surface{0x11, 0x11, 0x14};
  fltr::Color surfaceRaised{0x18, 0x18, 0x1d};
  fltr::Color border{0x26, 0x26, 0x2d};
  fltr::Color borderStrong{0x39, 0x39, 0x44};
  fltr::Color text{0xec, 0xec, 0xef};
  fltr::Color textDim{0x8b, 0x8b, 0x99};
  fltr::Color textFaint{0x5a, 0x5a, 0x66};
  fltr::Color accent{0x6e, 0x9b, 0xff};
  fltr::Color accentSoft{0x24, 0x33, 0x59};
  fltr::Color danger{0xff, 0x6b, 0x6b};
  fltr::Color ok{0x57, 0xd9, 0xa3};

  float radius = 6.0f;
  float hairline = 1.0f;
  float unit = 8.0f;
  float fontSize = 14.0f;

  /// Whether the player is driving with the keyboard, which is the only thing
  /// focus rings are shown for. Null means "always show them", which is what a
  /// consumer that does not track input modality gets.
  fltr::ValueListenable<bool>* keyboardMode = nullptr;

  friend constexpr bool operator==(const Theme&, const Theme&) noexcept = default;
};

using ThemeScope = fltr::Ambient<Theme>;

/// Text styles are derived rather than stored: fltr's `Text` takes a full
/// `TextStyle` at every call site (there is no ambient default), so the theme
/// hands out complete ones and nothing repeats a font size by hand.
fltr::TextStyle bodyStyle(const Theme& theme);
fltr::TextStyle labelStyle(const Theme& theme);
fltr::TextStyle titleStyle(const Theme& theme);
fltr::TextStyle displayStyle(const Theme& theme);
fltr::TextStyle monoStyle(const Theme& theme, fltr::Color color);

/// Surfaces, as decorations. `BoxDecoration` is fill + radius + border and
/// nothing else, so "sleek" here means restraint: hairline borders, small radii,
/// and contrast carried by the border rather than by a fill.
fltr::BoxDecoration panelDecoration(const Theme& theme);
fltr::BoxDecoration insetDecoration(const Theme& theme);
fltr::BoxDecoration scrimDecoration(const Theme& theme);

}  // namespace demo
