#include "ui/theme.hpp"

#include <algorithm>

namespace fltrdemo {

Theme Theme::forSurface(fltr::Size surface) noexcept {
  Theme theme;
  // The design size, and the range outside which scaling stops helping: below
  // the floor the text stops being readable, above the ceiling the menu turns
  // into a poster.
  const float byWidth = surface.width / 1280.0f;
  const float byHeight = surface.height / 800.0f;
  theme.scale = std::clamp(std::min(byWidth, byHeight), 0.62f, 1.75f);
  return theme;
}

}  // namespace fltrdemo
