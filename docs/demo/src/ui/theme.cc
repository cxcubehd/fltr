#include "ui/theme.hh"

namespace demo {

using fltr::BorderRadius;
using fltr::BoxDecoration;
using fltr::Color;
using fltr::TextStyle;

namespace {

TextStyle styled(float size, Color color, float tracking) {
  return TextStyle{
      .font = 0,
      .size = size,
      .color = color,
      .letterSpacing = tracking,
      .lineHeight = 1.35f,
  };
}

}  // namespace

TextStyle bodyStyle(const Theme& theme) {
  return styled(theme.fontSize, theme.text, 0.0f);
}

TextStyle labelStyle(const Theme& theme) {
  return styled(theme.fontSize - 2.0f, theme.textDim, 0.6f);
}

TextStyle titleStyle(const Theme& theme) {
  return styled(theme.fontSize + 4.0f, theme.text, 0.0f);
}

TextStyle displayStyle(const Theme& theme) {
  return styled(theme.fontSize + 16.0f, theme.text, -0.4f);
}

TextStyle monoStyle(const Theme& theme, Color color) {
  return styled(theme.fontSize, color, 0.4f);
}

BoxDecoration panelDecoration(const Theme& theme) {
  return BoxDecoration{
      .color = theme.surface,
      .radius = BorderRadius::all(theme.radius),
      .borderColor = theme.border,
      .borderWidth = theme.hairline,
  };
}

BoxDecoration insetDecoration(const Theme& theme) {
  return BoxDecoration{
      .color = theme.background,
      .radius = BorderRadius::all(theme.radius - 2.0f),
      .borderColor = theme.border,
      .borderWidth = theme.hairline,
  };
}

BoxDecoration scrimDecoration(const Theme& theme) {
  return BoxDecoration{.color = theme.background.withAlpha(0xcc)};
}

}  // namespace demo
