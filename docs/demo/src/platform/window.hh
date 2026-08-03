#pragma once

#include "fltr/core/geometry.hpp"
#include "fltr/gestures/events.hpp"

namespace demo {

/// Everything the settings screen can change. Equality-comparable so the app can
/// apply it only when it actually moved -- raylib's window calls are not free.
struct GraphicsSettings {
  bool fullscreen = false;
  bool vsync = true;
  /// 0 means uncapped. The slider offers a handful of stops rather than a
  /// continuous range, which is what `RawSlider`'s `divisions` is for.
  int maxFps = 144;

  friend constexpr bool operator==(const GraphicsSettings&,
                                   const GraphicsSettings&) noexcept = default;
};

/// The window, and the only thing in the demo that owns one. fltr never does:
/// the surface size is pushed in, and the cursor is read out once a frame.
class Window {
public:
  void open(int width, int height, const char* title, const GraphicsSettings& settings);
  void close();

  bool shouldClose() const;
  fltr::Size surface() const;

  /// Physical pixels per logical pixel on the surface being drawn to, which the
  /// interface is scaled by before the player's own preference is applied. One
  /// everywhere the demo has no way to ask; in a browser it is the device pixel
  /// ratio exactly.
  float contentScale() const;

  /// Applies whatever differs from what is already applied.
  void apply(const GraphicsSettings& settings);
  const GraphicsSettings& settings() const noexcept { return settings_; }

  void applyCursor(fltr::MouseCursor cursor);

private:
  GraphicsSettings settings_;
  fltr::MouseCursor cursor_ = fltr::MouseCursor::Basic;
  bool open_ = false;
};

}  // namespace demo
