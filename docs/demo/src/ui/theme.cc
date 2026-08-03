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

Theme scaledTheme(float scale, fltr::ValueListenable<bool>* keyboardMode) {
  Theme theme;
  theme.radius *= scale;
  theme.hairline *= scale;
  theme.unit *= scale;
  theme.fontSize *= scale;
  theme.scale = scale;
  theme.keyboardMode = keyboardMode;
  return theme;
}

TextStyle bodyStyle(const Theme& theme) {
  return styled(theme.fontSize, theme.text, 0.0f);
}

// The steps are ratios rather than offsets so that a scaled theme keeps its
// typographic proportions instead of flattening as it grows.
TextStyle labelStyle(const Theme& theme) {
  return styled(theme.fontSize * 0.8f, theme.textDim, 0.6f);
}

TextStyle titleStyle(const Theme& theme) {
  return styled(theme.fontSize * 1.3f, theme.text, 0.0f);
}

TextStyle displayStyle(const Theme& theme) {
  return styled(theme.fontSize * 2.0f, theme.text, -0.4f);
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
      .radius = BorderRadius::all(theme.radius - theme.px(2.0f)),
      .borderColor = theme.border,
      .borderWidth = theme.hairline,
  };
}

BoxDecoration scrimDecoration(const Theme& theme) {
  return BoxDecoration{.color = theme.background.withAlpha(0xcc)};
}

}  // namespace demo
