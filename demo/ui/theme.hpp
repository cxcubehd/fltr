#pragma once

#include "fltr/paint/text.hpp"
#include "fltr/render/boxes.hpp"
#include "fltr/widgets/reactive.hpp"

namespace fltrdemo {

/// Every colour and every dimension in the demo comes from here.
///
/// That is not tidiness: it is what makes "nothing in ui/ or the pages contains
/// a hardcoded window dimension" checkable. A page asks the theme for a unit,
/// and the theme is derived from the surface, so resizing changes one value and
/// the layout protocol does the rest.
///
/// It is an ambient value, so changing it rebuilds exactly the elements whose
/// last build read it -- not the tree between.
struct Theme {
  float scale = 1.0f;
  fltr::FontHandle font = 0;

  fltr::Color background = fltr::Color::argb(0xFF0F1310);
  fltr::Color surface = fltr::Color::argb(0xFF1C231A);
  fltr::Color surfaceRaised = fltr::Color::argb(0xFF2A3326);
  fltr::Color surfaceSunken = fltr::Color::argb(0xFF141A13);
  fltr::Color border = fltr::Color::argb(0xFF44523C);
  fltr::Color borderBright = fltr::Color::argb(0xFF6F8163);
  fltr::Color accent = fltr::Color::argb(0xFFD8A02A);
  fltr::Color accentDim = fltr::Color::argb(0xFF6E5620);
  fltr::Color text = fltr::Color::argb(0xFFD3DACC);
  fltr::Color textBright = fltr::Color::argb(0xFFF3F6EE);
  fltr::Color textDim = fltr::Color::argb(0xFF8B9782);
  fltr::Color selection = fltr::Color::argb(0xFF3B4B2D);
  fltr::Color danger = fltr::Color::argb(0xFFC0503A);
  fltr::Color good = fltr::Color::argb(0xFF8FBF4A);

  /// The spacing unit everything else is a multiple of.
  constexpr float unit() const noexcept { return 8.0f * scale; }
  constexpr float radius() const noexcept { return 2.0f * scale; }
  constexpr float hairline() const noexcept { return scale < 1.0f ? 1.0f : scale; }
  constexpr float rowHeight() const noexcept { return 26.0f * scale; }
  constexpr float controlHeight() const noexcept { return 34.0f * scale; }
  constexpr float menuButtonWidth() const noexcept { return 260.0f * scale; }
  constexpr float tabWidth() const noexcept { return 128.0f * scale; }
  constexpr float sliderWidth() const noexcept { return 220.0f * scale; }
  constexpr float knobWidth() const noexcept { return 12.0f * scale; }

  fltr::TextStyle title() const noexcept { return styled(46.0f, textBright); }
  fltr::TextStyle heading() const noexcept { return styled(19.0f, accent); }
  fltr::TextStyle body() const noexcept { return styled(16.0f, text); }
  fltr::TextStyle label() const noexcept { return styled(14.0f, textDim); }
  fltr::TextStyle row() const noexcept { return styled(15.0f, text); }

  fltr::BoxDecoration panel() const noexcept {
    return {.color = surface, .radius = fltr::BorderRadius::all(radius()),
            .borderColor = border, .borderWidth = hairline()};
  }

  /// Derived from the surface, and the only place the window's size turns into
  /// a number anything else reads.
  static Theme forSurface(fltr::Size surface) noexcept;

  friend bool operator==(const Theme&, const Theme&) noexcept = default;

private:
  fltr::TextStyle styled(float size, fltr::Color color) const noexcept {
    return {.font = font, .size = size * scale, .color = color};
  }
};

static_assert(std::is_trivially_destructible_v<Theme>,
              "an ambient value is copied into the build arena");

using ThemeScope = fltr::Ambient<Theme>;

inline const Theme& themeOf(fltr::BuildContext& context) { return ThemeScope::of(context); }

}  // namespace fltrdemo
